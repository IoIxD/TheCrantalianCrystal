#pragma once

#include "systray_protocol.hpp"

#include <iconlib.hpp>

#include <Mw/Milsko.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

class TCCSystrayClient {
  static constexpr int ICON_SIZE = 22;
  static constexpr int ICON_SPACING = 4;

  struct IconWidget {
    TCCSystrayClient *client;
    MwWidget image;
    MwPixmap pixmap;
    // The item it shows.
    SystrayProtocol *protocol;
    std::string id;
  };

  MwRect mBounds;
  MwWidget mWindow;
  IconManager mIcons;
  std::vector<std::unique_ptr<SystrayProtocol>> mProtocols;
  // Pointers, since the widgets' mouse handlers hold on to them.
  std::vector<std::unique_ptr<IconWidget>> mIconWidgets;
  // Pixmaps of destroyed widgets, freed once Milsko has actually freed the
  // widgets on the next step.
  std::vector<MwPixmap> mOldPixmaps;
  bool mNeedsRelayout = false;

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

  void addProtocol(std::unique_ptr<SystrayProtocol> protocol);
  void itemsChanged(SystrayProtocol &protocol,
                    const std::vector<SystrayItem> &items);
  void relayout();
  // Returns malloc()'d RGBA pixels for the icon, or null.
  unsigned char *loadIcon(const SystrayIcon &icon, int size, int *width,
                          int *height);
  // Draws the item's overlay icon over the bottom right corner of pixels.
  void drawOverlay(const SystrayIcon &overlay, unsigned char *pixels,
                   int width, int height);

  static void MWAPI iconMouseDown(MwWidget handle, void *user, void *call);
  static void MWAPI iconMouseUp(MwWidget handle, void *user, void *call);
  // Screen position of a point in an icon widget.
  void screenPosition(IconWidget &icon, const MwMouse &mouse, int *x, int *y);
  static void MWAPI windowMouseUp(MwWidget handle, void *user, void *call);

  void showMenu(SystrayProtocol &protocol, const std::string &id,
                const std::vector<SystrayMenuEntry> &entries);
  void addMenuEntries(MwMenu parent,
                      const std::vector<SystrayMenuEntry> &entries);
  void closeMenu();
  static void MWAPI menuChosen(MwWidget handle, void *user, void *call);

public:
  TCCSystrayClient();
  void run();
};

#ifdef __cplusplus
extern "C" {
#endif

MwWidget window_setup(MwRect *bounds);

#ifdef __cplusplus
}
#endif
