#ifdef TCC_SYSTRAY_RIVER
#include "river_keyboard_layout.hpp"

#include <river-input-management-v1-protocol.h>
#include <river-xkb-config-v1-protocol.h>
#include <wayland-client.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <poll.h>

namespace {

constexpr const char *ITEM_ID = "keyboard";
// Layouts are LAYOUT_ENTRY + their index.
constexpr int LAYOUT_ENTRY = 100;

// Splits a comma separated environment variable, keeping empty parts so that
// indexes line up (XKB_DEFAULT_VARIANT is often ",nodeadkeys").
std::vector<std::string> splitEnv(const char *var) {
  std::vector<std::string> parts;
  const char *value = getenv(var);
  if (!value || !*value)
    return parts;
  std::string current;
  for (const char *c = value;; c++) {
    if (*c == ',' || !*c) {
      parts.push_back(current);
      current.clear();
      if (!*c)
        break;
    } else {
      current += *c;
    }
  }
  return parts;
}

// Events we don't need.
void ignoreInputDevice(void *, river_xkb_keyboard_v1 *, river_input_device_v1 *) {}
void ignoreLock(void *, river_xkb_keyboard_v1 *) {}

} // namespace

RiverKeyboardLayout::~RiverKeyboardLayout() {
  setItemsChangedCallback(nullptr);
  disconnect();
}

bool RiverKeyboardLayout::connect() {
  mDisplay = wl_display_connect(nullptr);
  if (!mDisplay)
    return false;

  static const wl_registry_listener registryListener = {
      registryGlobal,
      registryGlobalRemove,
  };
  mRegistry = wl_display_get_registry(mDisplay);
  wl_registry_add_listener(mRegistry, &registryListener, this);
  // Once for the globals, once more for the keyboards and their layouts.
  wl_display_roundtrip(mDisplay);
  if (!mConfig) {
    disconnect();
    return false;
  }
  wl_display_roundtrip(mDisplay);

  mLayouts = splitEnv("XKB_DEFAULT_LAYOUT");
  mVariants = splitEnv("XKB_DEFAULT_VARIANT");
  update();
  return true;
}

void RiverKeyboardLayout::disconnect() {
  if (!mDisplay)
    return;
  for (river_xkb_keyboard_v1 *keyboard : mKeyboards)
    river_xkb_keyboard_v1_destroy(keyboard);
  mKeyboards.clear();
  // river_xkb_config_v1 may only be destroyed after stop/finished, which
  // doesn't matter when the whole connection goes away.
  if (mRegistry)
    wl_registry_destroy(mRegistry);
  wl_display_disconnect(mDisplay);
  mDisplay = nullptr;
  mRegistry = nullptr;
  mConfig = nullptr;
}

int RiverKeyboardLayout::fd() const {
  return mDisplay ? wl_display_get_fd(mDisplay) : -1;
}

void RiverKeyboardLayout::poll() {
  if (!mDisplay)
    return;

  // Reads whatever the compositor sent, without blocking.
  while (wl_display_prepare_read(mDisplay) != 0)
    wl_display_dispatch_pending(mDisplay);
  wl_display_flush(mDisplay);

  pollfd pfd = {wl_display_get_fd(mDisplay), POLLIN, 0};
  if (::poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
    if (wl_display_read_events(mDisplay) < 0) {
      disconnect();
      mItems.clear();
      itemsChanged();
      return;
    }
  } else {
    wl_display_cancel_read(mDisplay);
  }
  wl_display_dispatch_pending(mDisplay);
}

void RiverKeyboardLayout::registryGlobal(void *data, wl_registry *registry,
                                         uint32_t name, const char *iface,
                                         uint32_t version) {
  (void)version;
  auto *self = static_cast<RiverKeyboardLayout *>(data);
  if (self->mConfig || strcmp(iface, river_xkb_config_v1_interface.name) != 0)
    return;

  static const river_xkb_config_v1_listener configListener = {
      configFinished,
      configKeyboard,
  };
  self->mConfig = static_cast<river_xkb_config_v1 *>(
      wl_registry_bind(registry, name, &river_xkb_config_v1_interface, 1));
  river_xkb_config_v1_add_listener(self->mConfig, &configListener, self);
}

void RiverKeyboardLayout::registryGlobalRemove(void *, wl_registry *,
                                               uint32_t) {}

void RiverKeyboardLayout::configFinished(void *data,
                                         river_xkb_config_v1 *config) {
  // river stopped sending events, e.g. while shutting down.
  auto *self = static_cast<RiverKeyboardLayout *>(data);
  river_xkb_config_v1_destroy(config);
  if (self->mConfig == config)
    self->mConfig = nullptr;
}

void RiverKeyboardLayout::configKeyboard(void *data, river_xkb_config_v1 *,
                                         river_xkb_keyboard_v1 *keyboard) {
  auto *self = static_cast<RiverKeyboardLayout *>(data);
  static const river_xkb_keyboard_v1_listener keyboardListener = {
      keyboardRemoved, ignoreInputDevice, keyboardLayout, ignoreLock,
      ignoreLock,      ignoreLock,        ignoreLock,
  };
  river_xkb_keyboard_v1_add_listener(keyboard, &keyboardListener, self);
  self->mKeyboards.push_back(keyboard);

  // The protocol promises a layout event for new keyboards, but river 0.4.8
  // only sends one once the layout changes. A keymap starts out on its first
  // layout, so assume that until told otherwise.
  if (!self->mKnown) {
    self->mCurrent = 0;
    self->mKnown = true;
    self->update();
  }
}

void RiverKeyboardLayout::keyboardRemoved(void *data,
                                          river_xkb_keyboard_v1 *keyboard) {
  auto *self = static_cast<RiverKeyboardLayout *>(data);
  std::erase(self->mKeyboards, keyboard);
  river_xkb_keyboard_v1_destroy(keyboard);
  if (self->mKeyboards.empty()) {
    self->mKnown = false;
    self->update();
  }
}

void RiverKeyboardLayout::keyboardLayout(void *data, river_xkb_keyboard_v1 *,
                                         uint32_t index, const char *name) {
  auto *self = static_cast<RiverKeyboardLayout *>(data);
  self->mCurrent = index;
  self->mKnown = true;
  if (name)
    self->mLayoutNames[index] = name;
  self->update();
}

size_t RiverKeyboardLayout::layoutCount() const {
  size_t count = mLayouts.size();
  // In case the keymap has more layouts than the environment says.
  for (const auto &[index, name] : mLayoutNames)
    count = std::max<size_t>(count, index + 1);
  if (mKnown)
    count = std::max<size_t>(count, mCurrent + 1);
  return count;
}

std::string RiverKeyboardLayout::shortName(uint32_t index) const {
  if (index < mLayouts.size() && !mLayouts[index].empty())
    return mLayouts[index];
  // Not in the environment, so the best we have is the name's start.
  auto name = mLayoutNames.find(index);
  if (name != mLayoutNames.end() && !name->second.empty())
    return name->second.substr(0, 2);
  return std::to_string(index + 1);
}

std::string RiverKeyboardLayout::displayName(uint32_t index) const {
  auto name = mLayoutNames.find(index);
  if (name != mLayoutNames.end() && !name->second.empty())
    return name->second;
  std::string label = shortName(index);
  if (index < mVariants.size() && !mVariants[index].empty())
    label += std::format(" ({})", mVariants[index]);
  return label;
}

void RiverKeyboardLayout::update() {
  std::vector<SystrayItem> items;

  // Nothing to switch between with a single layout.
  if (mKnown && layoutCount() > 1) {
    SystrayItem item;
    item.id = ITEM_ID;
    item.title = std::format("Keyboard layout: {}", displayName(mCurrent));
    item.icon.text = shortName(mCurrent);
    items.push_back(std::move(item));
  }

  if (items == mItems)
    return;
  mItems = std::move(items);
  itemsChanged();
}

void RiverKeyboardLayout::setLayout(uint32_t index) {
  // Every keyboard, so they stay in step.
  for (river_xkb_keyboard_v1 *keyboard : mKeyboards)
    river_xkb_keyboard_v1_set_layout_by_index(keyboard, (int32_t)index);
  if (mDisplay)
    wl_display_flush(mDisplay);
}

void RiverKeyboardLayout::activate(const std::string &id, int x, int y) {
  (void)id;
  (void)x;
  (void)y;
  size_t count = layoutCount();
  if (count > 1)
    setLayout((mCurrent + 1) % count);
}

void RiverKeyboardLayout::contextMenu(const std::string &id, int x, int y) {
  (void)x;
  (void)y;
  if (mItems.empty())
    return;

  std::vector<SystrayMenuEntry> entries;
  for (uint32_t i = 0; i < layoutCount(); i++) {
    SystrayMenuEntry entry;
    entry.id = LAYOUT_ENTRY + (int)i;
    entry.label = displayName(i);
    entry.toggle = SystrayMenuEntry::Toggle::Radio;
    entry.toggled = i == mCurrent;
    entries.push_back(entry);
  }
  showMenu(id, entries);
}

void RiverKeyboardLayout::menuEntryActivated(const std::string &id,
                                             int entryId) {
  (void)id;
  int index = entryId - LAYOUT_ENTRY;
  if (index >= 0 && index < (int)layoutCount())
    setLayout(index);
}
#endif
