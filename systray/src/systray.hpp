#pragma once

#include "systray_protocol.hpp"
#include "windows/mixer_window.hpp"

#include <iconlib.hpp>

#include <Mw/Milsko.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

class TCCSystrayClient {
  static constexpr int ICON_SIZE = 22;
  static constexpr int ICON_SPACING = 4;
  static constexpr int BUTTON_SIZE = 32;
  static constexpr int NOTCH_WIDTH = 14;

  struct IconWidget {
    TCCSystrayClient *client;
    // The MwButton showing the icon (or its text).
    MwWidget image;
    // Null for text icons.
    MwPixmap pixmap;
    // The item it shows.
    SystrayProtocol *protocol;
    std::string id;
  };

  MwRect mBounds;
  MwWidget mWindow;
  // An MwFrame after the last icon, drawn by drawNotch.
  MwWidget mNotch;
  IconManager mIcons;
  std::vector<std::unique_ptr<SystrayProtocol>> mProtocols;
  // Pointers, since the widgets' mouse handlers hold on to them.
  std::vector<std::unique_ptr<IconWidget>> mIconWidgets;
  // Pixmaps of destroyed widgets, freed once Milsko has actually freed the
  // widgets on the next step.
  std::vector<MwPixmap> mOldPixmaps;
  bool mNeedsRelayout = false;
  bool mCollapsed = false;

  // The item menu being shown, if any.
  struct OpenMenu {
    SystrayProtocol *protocol;
    // The item it belongs to.
    std::string id;
    // Hidden, 1x1 widget above the item's icon that the menu opens from.
    MwWidget holder;
    // Hidden menu bar inside holder, used to build the MwMenu tree that the
    // popup (an MwSubMenu) shows. Destroying it frees the tree.
    MwWidget menubar;
    // Protocol entry ids of the entries that can be chosen.
    std::map<MwMenu, int> entryIds;
  };
  std::unique_ptr<OpenMenu> mMenu;

  // The item mixer being shown, if any.
  struct OpenMixer {
    SystrayProtocol *protocol;
    // The item it belongs to.
    std::string id;
    std::unique_ptr<MixerWindow> window;
  };
  std::unique_ptr<OpenMixer> mMixer;

  void addProtocol(std::unique_ptr<SystrayProtocol> protocol);
  // Starts a program that isn't tied to the systray's lifetime.
  static void launchDetached(std::vector<const char *> argv);
  void itemsChanged(SystrayProtocol &protocol,
                    const std::vector<SystrayItem> &items);
  void relayout();
  // Returns malloc()'d RGBA pixels for the icon, or null.
  unsigned char *loadIcon(const SystrayIcon &icon, int size, int *width,
                          int *height);
  // Draws the item's overlay icon over the bottom right corner of pixels.
  void drawOverlay(const SystrayIcon &overlay, unsigned char *pixels, int width,
                   int height);

  static void MWAPI iconMouseDown(MwWidget handle, void *user, void *call);
  static void MWAPI iconMouseUp(MwWidget handle, void *user, void *call);
  // Screen position of a point in an icon widget.
  void screenPosition(IconWidget &icon, const MwMouse &mouse, int *x, int *y);
  static void MWAPI windowMouseUp(MwWidget handle, void *user, void *call);
  static void MWAPI drawNotch(MwWidget handle, void *user, void *call);
  // Clicking the notch collapses the bar to just the notch, or expands it.
  static void MWAPI notchMouseUp(MwWidget handle, void *user, void *call);

  void showMenu(SystrayProtocol &protocol, const std::string &id,
                const std::vector<SystrayMenuEntry> &entries);
  void addMenuEntries(MwMenu parent,
                      const std::vector<SystrayMenuEntry> &entries);
  void closeMenu();
  static void MWAPI menuChosen(MwWidget handle, void *user, void *call);

  void showMixer(SystrayProtocol &protocol, const std::string &id,
                 const SystrayMixer &mixer);
  void closeMixer();
  // Closes whichever of the menu and mixer is open. Returns whether it
  // belonged to the given item.
  bool closePopups(SystrayProtocol *protocol, const std::string &id);

public:
  TCCSystrayClient(bool has_dbus, bool has_pulse, bool has_wl);
  void run();
};

#ifdef __cplusplus
extern "C" {
#endif

MwWidget window_setup(MwRect *bounds);
/* Turns a child widget into a popup at x, y relative to its parent. */
void popup_setup(MwWidget widget, int x, int y);

#ifdef __cplusplus
}
#endif
