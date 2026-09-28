#ifdef TCC_SYSTRAY_PULSE
#include "pulse_volume.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <string_view>

namespace {

constexpr int SCROLL_STEP = 5;
constexpr int LEVELS[] = {100, 75, 50, 25};

// Menu entry ids.
constexpr int MUTE_ENTRY = 1;
// Levels are LEVEL_ENTRY + their percentage.
constexpr int LEVEL_ENTRY = 100;
// Devices are DEVICE_ENTRY + their index.
constexpr int DEVICE_ENTRY = 1000;

} // namespace

PulseVolume::~PulseVolume() {
  setItemsChangedCallback(nullptr);
  destroyContext();
  if (mMainloop)
    mLib->pa_mainloop_free(mMainloop);
}

bool PulseVolume::connect() {
  mLib = PulseLib::get();
  if (!mLib)
    return false;

  mMainloop = mLib->pa_mainloop_new();
  if (!mMainloop)
    return false;
  createContext();
  return mContext != nullptr;
}

void PulseVolume::createContext() {
  mContext = mLib->pa_context_new(mLib->pa_mainloop_get_api(mMainloop),
                                  "tcc_systray");
  if (!mContext)
    return;
  mLib->pa_context_set_state_callback(mContext, stateCallback, this);
  // NOFAIL waits for the server to appear instead of failing if it isn't
  // running yet.
  if (mLib->pa_context_connect(mContext, nullptr, PA_CONTEXT_NOFAIL,
                               nullptr) < 0)
    destroyContext();
}

void PulseVolume::destroyContext() {
  if (!mContext)
    return;
  mLib->pa_context_set_state_callback(mContext, nullptr, nullptr);
  mLib->pa_context_set_subscribe_callback(mContext, nullptr, nullptr);
  mLib->pa_context_disconnect(mContext);
  mLib->pa_context_unref(mContext);
  mContext = nullptr;
  mRefreshing = false;
  mRefreshAgain = false;
}

void PulseVolume::poll() {
  if (!mMainloop)
    return;

  // Dispatches whatever is ready, without blocking. Bounded, in case events
  // keep arriving.
  for (int i = 0; i < 64; i++) {
    if (mLib->pa_mainloop_iterate(mMainloop, 0, nullptr) <= 0)
      break;
  }

  if (mReconnect) {
    mReconnect = false;
    destroyContext();
    createContext();
  }
}

void PulseVolume::stateCallback(pa_context *context, void *data) {
  auto *self = static_cast<PulseVolume *>(data);
  PulseLib *p = self->mLib;

  switch (p->pa_context_get_state(context)) {
  case PA_CONTEXT_READY: {
    // The server's events tell us when the default device changes. For
    // inputs, source outputs are the streams apps record with.
    auto devices = self->mDirection == Direction::Output
                       ? PA_SUBSCRIPTION_MASK_SINK
                       : (pa_subscription_mask_t)(PA_SUBSCRIPTION_MASK_SOURCE |
                                                  PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT);
    p->pa_context_set_subscribe_callback(context, subscribeCallback, self);
    self->finish(p->pa_context_subscribe(
        context,
        (pa_subscription_mask_t)(devices | PA_SUBSCRIPTION_MASK_SERVER),
        nullptr, nullptr));
    self->refresh();
    break;
  }
  case PA_CONTEXT_FAILED:
  case PA_CONTEXT_TERMINATED:
    // e.g. the server restarted. Can't destroy the context from its own
    // callback, so that's left to poll().
    self->mDevices.clear();
    self->mDefaultDevice.clear();
    self->mRecording = false;
    self->update();
    self->mReconnect = true;
    break;
  default:
    break;
  }
}

void PulseVolume::subscribeCallback(pa_context *, pa_subscription_event_type_t,
                                    uint32_t, void *data) {
  static_cast<PulseVolume *>(data)->refresh();
}

// Fetches the default device's name, then every device, then for inputs
// whether anything is recording.
void PulseVolume::refresh() {
  if (!mContext)
    return;
  if (mRefreshing) {
    mRefreshAgain = true;
    return;
  }
  mRefreshing = true;
  mRefreshAgain = false;
  finish(mLib->pa_context_get_server_info(mContext, serverInfoCallback, this));
}

void PulseVolume::serverInfoCallback(pa_context *context,
                                     const pa_server_info *info, void *data) {
  auto *self = static_cast<PulseVolume *>(data);
  PulseLib *p = self->mLib;
  bool output = self->mDirection == Direction::Output;

  const char *name = nullptr;
  if (info)
    name = output ? info->default_sink_name : info->default_source_name;
  self->mDefaultDevice = name ? name : "";
  self->mNewDevices.clear();

  if (output)
    self->finish(
        p->pa_context_get_sink_info_list(context, sinkInfoCallback, self));
  else
    self->finish(
        p->pa_context_get_source_info_list(context, sourceInfoCallback, self));
}

void PulseVolume::sinkInfoCallback(pa_context *, const pa_sink_info *info,
                                   int eol, void *data) {
  auto *self = static_cast<PulseVolume *>(data);
  if (!eol && info)
    self->addDevice(info->index, info->name, info->description, info->volume,
                    info->mute);
  else
    self->devicesDone();
}

void PulseVolume::sourceInfoCallback(pa_context *, const pa_source_info *info,
                                     int eol, void *data) {
  auto *self = static_cast<PulseVolume *>(data);
  // Monitors (what an output is playing) are sources too, but not inputs.
  if (!eol && info) {
    if (info->monitor_of_sink == PA_INVALID_INDEX)
      self->addDevice(info->index, info->name, info->description, info->volume,
                      info->mute);
  } else {
    self->devicesDone();
  }
}

void PulseVolume::sourceOutputInfoCallback(pa_context *,
                                           const pa_source_output_info *info,
                                           int eol, void *data) {
  auto *self = static_cast<PulseVolume *>(data);
  if (eol || !info) {
    self->mRecording = self->mNewRecording;
    self->refreshDone();
    return;
  }

  // Only recording from a real input counts (mDevices has no monitors), and
  // not the level meters that volume mixers keep open.
  static constexpr const char *METERS[] = {
      "org.PulseAudio.pavucontrol", "org.kde.plasma-pa", "org.kde.kmixd",
      "org.gnome.VolumeControl", "org.gnome.Settings"};
  const char *app =
      self->mLib->pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_ID);
  bool meter = app && std::any_of(std::begin(METERS), std::end(METERS),
                                  [&](const char *m) {
                                    return std::string_view(app) == m;
                                  });
  bool fromInput =
      std::any_of(self->mDevices.begin(), self->mDevices.end(),
                  [&](const Device &d) { return d.index == info->source; });
  if (fromInput && !meter)
    self->mNewRecording = true;
}

void PulseVolume::addDevice(uint32_t index, const char *name,
                            const char *description, const pa_cvolume &volume,
                            bool muted) {
  Device device;
  device.index = index;
  device.name = name ? name : "";
  device.description = description ? description : device.name;
  device.volume = volume;
  device.muted = muted;
  mNewDevices.push_back(std::move(device));
}

// The end of the device list, or an error.
void PulseVolume::devicesDone() {
  mDevices = std::move(mNewDevices);
  mNewDevices.clear();

  if (mDirection == Direction::Input && mContext) {
    mNewRecording = false;
    pa_operation *op = mLib->pa_context_get_source_output_info_list(
        mContext, sourceOutputInfoCallback, this);
    if (op) {
      finish(op);
      return;
    }
  }
  refreshDone();
}

void PulseVolume::refreshDone() {
  mRefreshing = false;
  update();
  if (mRefreshAgain)
    refresh();
}

void PulseVolume::finish(pa_operation *op) {
  // Operations complete on their own; we only need to drop our reference.
  if (op)
    mLib->pa_operation_unref(op);
}

const PulseVolume::Device *PulseVolume::defaultDevice() const {
  auto device =
      std::find_if(mDevices.begin(), mDevices.end(),
                   [&](const Device &d) { return d.name == mDefaultDevice; });
  if (device == mDevices.end())
    return mDevices.empty() ? nullptr : &mDevices.front();
  return &*device;
}

int PulseVolume::percent(const Device &device) const {
  return (int)std::lround(mLib->pa_cvolume_avg(&device.volume) * 100.0 /
                          PA_VOLUME_NORM);
}

void PulseVolume::update() {
  std::vector<SystrayItem> items;
  bool output = mDirection == Direction::Output;

  const Device *device = defaultDevice();
  // Like Plasma, the microphone is only shown while it's in use.
  if (!output && !mRecording)
    device = nullptr;

  if (device) {
    int level = percent(*device);

    SystrayItem item;
    item.id = output ? "volume" : "microphone";
    const char *what = output ? "Volume" : "Microphone";
    item.title = device->muted ? std::format("{}: muted", what)
                               : std::format("{}: {}%", what, level);

    const char *name = device->muted || level == 0 ? "muted"
                       : level < 34                ? "low"
                       : level < 67                ? "medium"
                                                   : "high";
    if (output) {
      item.icon.name = std::format("audio-volume-{}", name);
      item.icon.fallbackNames = {std::format("audio-volume-{}-symbolic", name),
                                 "audio-card"};
      // Breeze warns about going over 100%.
      if (!device->muted && level > 100) {
        item.icon.fallbackNames.insert(item.icon.fallbackNames.begin(),
                                       item.icon.name);
        item.icon.name = "audio-volume-high-warning";
      }
    } else {
      item.icon.name = std::format("microphone-sensitivity-{}", name);
      item.icon.fallbackNames = {
          std::format("microphone-sensitivity-{}-symbolic", name),
          "audio-input-microphone", "audio-input-microphone-symbolic"};
    }

    items.push_back(std::move(item));
  }

  if (items == mItems)
    return;
  mItems = std::move(items);
  itemsChanged();
}

void PulseVolume::setMuted(bool muted) {
  const Device *device = defaultDevice();
  if (!device || !mContext)
    return;
  const char *name = device->name.c_str();
  if (mDirection == Direction::Output)
    finish(mLib->pa_context_set_sink_mute_by_name(mContext, name, muted,
                                                  nullptr, nullptr));
  else
    finish(mLib->pa_context_set_source_mute_by_name(mContext, name, muted,
                                                    nullptr, nullptr));
}

void PulseVolume::setPercent(int percent) {
  const Device *device = defaultDevice();
  if (!device || !mContext)
    return;
  // Scales every channel, keeping the balance between them.
  pa_cvolume volume = device->volume;
  mLib->pa_cvolume_scale(&volume,
                         (pa_volume_t)((uint64_t)PA_VOLUME_NORM * percent / 100));
  const char *name = device->name.c_str();
  if (mDirection == Direction::Output)
    finish(mLib->pa_context_set_sink_volume_by_name(mContext, name, &volume,
                                                    nullptr, nullptr));
  else
    finish(mLib->pa_context_set_source_volume_by_name(mContext, name, &volume,
                                                      nullptr, nullptr));
}

void PulseVolume::setDefault(const std::string &name) {
  if (!mContext)
    return;
  if (mDirection == Direction::Output)
    finish(mLib->pa_context_set_default_sink(mContext, name.c_str(), nullptr,
                                             nullptr));
  else
    finish(mLib->pa_context_set_default_source(mContext, name.c_str(), nullptr,
                                               nullptr));
}

void PulseVolume::activate(const std::string &id, int x, int y) {
  contextMenu(id, x, y);
}

void PulseVolume::secondaryActivate(const std::string &id, int x, int y) {
  (void)id;
  (void)x;
  (void)y;
  if (const Device *device = defaultDevice())
    setMuted(!device->muted);
}

void PulseVolume::scroll(const std::string &id, int delta, bool horizontal) {
  (void)id;
  const Device *device = defaultDevice();
  if (!device || horizontal)
    return;
  // Scrolling stops at 100%, going over it has to be deliberate.
  int current = percent(*device);
  int target = std::clamp(current + delta * SCROLL_STEP, 0,
                          std::max(current, 100));
  if (target != current)
    setPercent(target);
}

void PulseVolume::contextMenu(const std::string &id, int x, int y) {
  (void)x;
  (void)y;
  const Device *device = defaultDevice();
  if (!device)
    return;
  int level = percent(*device);

  std::vector<SystrayMenuEntry> entries;

  SystrayMenuEntry status;
  status.label = device->muted
                     ? std::format("{}: muted", device->description)
                     : std::format("{}: {}%", device->description, level);
  status.enabled = false;
  entries.push_back(status);

  SystrayMenuEntry separator;
  separator.type = SystrayMenuEntry::Type::Separator;
  entries.push_back(separator);

  SystrayMenuEntry mute;
  mute.id = MUTE_ENTRY;
  mute.label = "Mute";
  mute.toggle = SystrayMenuEntry::Toggle::Checkmark;
  mute.toggled = device->muted;
  entries.push_back(mute);

  // A few fixed levels, since menus can't hold a slider.
  entries.push_back(separator);
  for (int preset : LEVELS) {
    SystrayMenuEntry entry;
    entry.id = LEVEL_ENTRY + preset;
    entry.label = std::format("{}%", preset);
    entry.toggle = SystrayMenuEntry::Toggle::Radio;
    entry.toggled = std::abs(level - preset) <= 2;
    entries.push_back(entry);
  }

  mMenuDevices.clear();
  if (mDevices.size() > 1) {
    SystrayMenuEntry devices;
    devices.label = mDirection == Direction::Output ? "Output" : "Input";
    for (size_t i = 0; i < mDevices.size(); i++) {
      SystrayMenuEntry entry;
      entry.id = DEVICE_ENTRY + (int)i;
      entry.label = mDevices[i].description;
      entry.toggle = SystrayMenuEntry::Toggle::Radio;
      entry.toggled = &mDevices[i] == device;
      devices.children.push_back(entry);
      mMenuDevices.push_back(mDevices[i].name);
    }
    entries.push_back(separator);
    entries.push_back(devices);
  }

  showMenu(id, entries);
}

void PulseVolume::menuEntryActivated(const std::string &id, int entryId) {
  (void)id;
  const Device *device = defaultDevice();
  if (!device || !mContext)
    return;

  if (entryId == MUTE_ENTRY) {
    setMuted(!device->muted);
  } else if (entryId >= DEVICE_ENTRY &&
             entryId - DEVICE_ENTRY < (int)mMenuDevices.size()) {
    setDefault(mMenuDevices[entryId - DEVICE_ENTRY]);
  } else if (entryId > LEVEL_ENTRY && entryId <= LEVEL_ENTRY + 100) {
    setPercent(entryId - LEVEL_ENTRY);
    // Picking a level implies wanting to hear it (or be heard).
    if (device->muted)
      setMuted(false);
  }
}
#endif
