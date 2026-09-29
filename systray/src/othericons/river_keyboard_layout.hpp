#ifdef TCC_SYSTRAY_RIVER
#pragma once

#include "../systray_protocol.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct WaylandLib;
struct wl_display;
struct wl_registry;
struct river_xkb_config_v1;
struct river_xkb_keyboard_v1;

// A built-in keyboard layout item for river, through its river_xkb_config_v1
// protocol. Shows the active layout's short name, and only when more than one
// layout is configured. There is no item outside of river.
//
// river only reports the active layout, never the whole list, so the list
// comes from XKB_DEFAULT_LAYOUT and XKB_DEFAULT_VARIANT, which is what river
// builds its default keymap from.
//
// Clicking it switches to the next layout, the menu picks one.
class RiverKeyboardLayout : public SystrayProtocol {
public:
  RiverKeyboardLayout() = default;
  ~RiverKeyboardLayout() override;

  const char *name() const override { return "river keyboard layout"; }
  // Returns false if there is no Wayland compositor or it isn't river.
  bool connect() override;
  void poll() override;
  int fd() const override;

  void activate(const std::string &id, int x, int y) override;
  void contextMenu(const std::string &id, int x, int y) override;
  void menuEntryActivated(const std::string &id, int entryId) override;

private:
  WaylandLib *mLib = nullptr;
  // Our own connection; Milsko's isn't available to us.
  wl_display *mDisplay = nullptr;
  wl_registry *mRegistry = nullptr;
  river_xkb_config_v1 *mConfig = nullptr;
  std::vector<river_xkb_keyboard_v1 *> mKeyboards;

  // From the environment, e.g. {"us", "de"} and {"", "nodeadkeys"}.
  std::vector<std::string> mLayouts;
  std::vector<std::string> mVariants;
  // Names river reported for layouts, by index, e.g. "English (US)".
  std::map<uint32_t, std::string> mLayoutNames;
  // The active layout of whichever keyboard last reported one.
  uint32_t mCurrent = 0;
  bool mKnown = false;

  void disconnect();
  void update();
  size_t layoutCount() const;
  std::string shortName(uint32_t index) const;
  std::string displayName(uint32_t index) const;
  void setLayout(uint32_t index);

  static void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                             const char *iface, uint32_t version);
  static void registryGlobalRemove(void *data, wl_registry *registry,
                                   uint32_t name);
  static void configFinished(void *data, river_xkb_config_v1 *config);
  static void configKeyboard(void *data, river_xkb_config_v1 *config,
                             river_xkb_keyboard_v1 *keyboard);
  static void keyboardRemoved(void *data, river_xkb_keyboard_v1 *keyboard);
  static void keyboardLayout(void *data, river_xkb_keyboard_v1 *keyboard,
                             uint32_t index, const char *name);
};
#endif
