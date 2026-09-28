#include "systray.hpp"
#ifdef TCC_SYSTRAY_DBUS
#include "dbus/sni_watcher.hpp"
#include "othericons/bluez_bluetooth.hpp"
#include "othericons/network_manager.hpp"
#include "othericons/upower_battery.hpp"
#endif
#ifdef TCC_SYSTRAY_PULSE
#include "othericons/pulse_volume.hpp"
#endif
#ifdef TCC_SYSTRAY_RIVER
#include "othericons/river_keyboard_layout.hpp"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>

TCCSystrayClient::TCCSystrayClient() {
  mWindow = window_setup(&mBounds);
  // Clicking the bar outside of the icons dismisses an open menu.
  MwAddUserHandler(mWindow, MwNmouseUpHandler, windowMouseUp, this);

#ifdef TCC_SYSTRAY_DBUS
  addProtocol(std::make_unique<StatusNotifierWatcher>());
  addProtocol(std::make_unique<BluezBluetooth>());
  addProtocol(std::make_unique<NetworkManagerIcon>());
  addProtocol(std::make_unique<UPowerBattery>());
#endif
#ifdef TCC_SYSTRAY_PULSE
  addProtocol(std::make_unique<PulseVolume>(PulseVolume::Direction::Input));
  addProtocol(std::make_unique<PulseVolume>(PulseVolume::Direction::Output));
#endif
#ifdef TCC_SYSTRAY_RIVER
  addProtocol(std::make_unique<RiverKeyboardLayout>());
#endif

  if (mProtocols.empty())
    fprintf(stderr, "tcc_systray: systray icons will not be available\n");
}

void TCCSystrayClient::addProtocol(std::unique_ptr<SystrayProtocol> protocol) {
  protocol->setIconSize(ICON_SIZE);
  protocol->setShowMenuCallback(
      [this](SystrayProtocol &protocol, const std::string &id,
             const std::vector<SystrayMenuEntry> &entries) {
        showMenu(protocol, id, entries);
      });
  protocol->setItemsChangedCallback(
      [this](SystrayProtocol &protocol, const std::vector<SystrayItem> &items) {
        itemsChanged(protocol, items);
      });
  if (!protocol->connect()) {
    fprintf(stderr, "tcc_systray: %s is unavailable\n", protocol->name());
    return;
  }
  mProtocols.push_back(std::move(protocol));
}

void TCCSystrayClient::itemsChanged(SystrayProtocol &protocol,
                                    const std::vector<SystrayItem> &items) {
  (void)protocol;
  (void)items;
  // Protocols can report several changes in one poll; lay out once after.
  mNeedsRelayout = true;
}

unsigned char *TCCSystrayClient::loadIcon(const SystrayIcon &icon, int size,
                                          int *width, int *height) {
  unsigned char *pixels = nullptr;
  *width = 0;
  *height = 0;

  if (!icon.name.empty())
    mIcons.get_icon_by_name(icon.name.c_str(), icon.themePath.c_str(), size,
                            width, height, &pixels);
  for (const auto &name : icon.fallbackNames) {
    if (pixels)
      break;
    if (!name.empty())
      mIcons.get_icon_by_name(name.c_str(), icon.themePath.c_str(), size,
                              width, height, &pixels);
  }
  if (!pixels && !icon.pixels.empty())
    mIcons.get_icon_from_pixels(icon.pixels.data(), icon.width, icon.height,
                                size, width, height, &pixels);
  return pixels;
}

void TCCSystrayClient::drawOverlay(const SystrayIcon &overlay,
                                   unsigned char *pixels, int width,
                                   int height) {
  if (overlay.empty())
    return;

  // Half the size of the icon, like other trays draw emblems.
  int overlayWidth, overlayHeight;
  unsigned char *src =
      loadIcon(overlay, std::max(1, std::min(width, height) / 2),
               &overlayWidth, &overlayHeight);
  if (!src)
    return;

  // Source-over blend of non-premultiplied RGBA.
  int offsetX = width - overlayWidth, offsetY = height - overlayHeight;
  for (int y = 0; y < overlayHeight; y++) {
    for (int x = 0; x < overlayWidth; x++) {
      const unsigned char *s = src + ((size_t)y * overlayWidth + x) * 4;
      unsigned char *d =
          pixels + ((size_t)(y + offsetY) * width + x + offsetX) * 4;

      int sa = s[3], da = d[3];
      int outA = sa + da * (255 - sa) / 255;
      if (outA == 0)
        continue;
      for (int c = 0; c < 3; c++)
        d[c] = (s[c] * sa + d[c] * da * (255 - sa) / 255) / outA;
      d[3] = outA;
    }
  }

  free(src);
}

void TCCSystrayClient::relayout() {
  // Don't leave a menu open for an item that went away.
  if (mMenu) {
    const auto &items = mMenu->protocol->items();
    if (std::none_of(items.begin(), items.end(), [&](const SystrayItem &item) {
          return item.id == mMenu->id;
        }))
      closeMenu();
  }

  for (auto &widget : mIconWidgets) {
    MwDestroyWidget(widget->image);
    if (widget->pixmap)
      mOldPixmaps.push_back(widget->pixmap);
  }
  mIconWidgets.clear();

  int x = ICON_SPACING;
  for (auto &protocol : mProtocols) {
    for (const auto &item : protocol->items()) {
      if (item.status == SystrayItem::Status::Passive)
        continue;

      if (!item.currentIcon().text.empty()) {
        MwWidget label = MwVaCreateWidget(
            MwLabelClass, NULL, mWindow, x, (32 - ICON_SIZE) / 2, ICON_SIZE,
            ICON_SIZE, MwNtext, item.currentIcon().text.c_str(), MwNalignment,
            MwALIGNMENT_CENTER, NULL);
        auto widget = std::make_unique<IconWidget>(
            IconWidget{this, label, nullptr, protocol.get(), item.id});
        MwAddUserHandler(label, MwNmouseDownHandler, iconMouseDown,
                         widget.get());
        MwAddUserHandler(label, MwNmouseUpHandler, iconMouseUp, widget.get());
        mIconWidgets.push_back(std::move(widget));
        x += ICON_SIZE + ICON_SPACING;
        continue;
      }

      int width, height;
      unsigned char *pixels =
          loadIcon(item.currentIcon(), ICON_SIZE, &width, &height);
      if (!pixels)
        mIcons.get_icon_by_name("application-x-executable", ICON_SIZE, &width,
                                &height, &pixels);
      if (!pixels)
        continue;
      drawOverlay(item.overlayIcon, pixels, width, height);

      MwPixmap pixmap = MwLoadRaw(mWindow, pixels, width, height);
      free(pixels);

      MwWidget image = MwVaCreateWidget(
          MwImageClass, NULL, mWindow, x + (ICON_SIZE - width) / 2,
          (32 - height) / 2, width, height, MwNpixmap, pixmap, NULL);
      auto widget = std::make_unique<IconWidget>(
          IconWidget{this, image, pixmap, protocol.get(), item.id});
      MwAddUserHandler(image, MwNmouseDownHandler, iconMouseDown, widget.get());
      MwAddUserHandler(image, MwNmouseUpHandler, iconMouseUp, widget.get());
      mIconWidgets.push_back(std::move(widget));
      x += ICON_SIZE + ICON_SPACING;
    }
  }

  MwVaApply(mWindow, MwNwidth,
            (mIconWidgets.size() * ICON_SIZE) + (ICON_SPACING * ICON_SIZE),
            MwNheight, 32, NULL);
}

void TCCSystrayClient::screenPosition(IconWidget &icon, const MwMouse &mouse,
                                      int *x, int *y) {
  *x = MwGetInteger(mWindow, MwNx) + MwGetInteger(icon.image, MwNx) +
       mouse.point.x;
  *y = MwGetInteger(mWindow, MwNy) + MwGetInteger(icon.image, MwNy) +
       mouse.point.y;
}

void MWAPI TCCSystrayClient::iconMouseDown(MwWidget handle, void *user,
                                           void *call) {
  (void)handle;
  auto &icon = *static_cast<IconWidget *>(user);
  auto &mouse = *static_cast<MwMouse *>(call);

  // Wheel "buttons" only need handling once, not on both press and release.
  if (mouse.button == MwMOUSE_WHEELUP)
    icon.protocol->scroll(icon.id, 1, false);
  else if (mouse.button == MwMOUSE_WHEELDOWN)
    icon.protocol->scroll(icon.id, -1, false);
}

void MWAPI TCCSystrayClient::iconMouseUp(MwWidget handle, void *user,
                                         void *call) {
  (void)handle;
  auto &icon = *static_cast<IconWidget *>(user);
  auto &mouse = *static_cast<MwMouse *>(call);

  TCCSystrayClient *client = icon.client;

  // Any click on the tray dismisses an open menu, and clicking the item whose
  // menu is open toggles it off rather than opening it again.
  if (client->mMenu) {
    bool sameItem = client->mMenu->protocol == icon.protocol &&
                    client->mMenu->id == icon.id;
    client->closeMenu();
    if (sameItem && mouse.button != MwMOUSE_MIDDLE)
      return;
  }

  int x, y;
  client->screenPosition(icon, mouse, &x, &y);
  switch (mouse.button) {
  case MwMOUSE_LEFT:
    icon.protocol->activate(icon.id, x, y);
    break;
  case MwMOUSE_MIDDLE:
    icon.protocol->secondaryActivate(icon.id, x, y);
    break;
  case MwMOUSE_RIGHT:
    icon.protocol->contextMenu(icon.id, x, y);
    break;
  }
}

void MWAPI TCCSystrayClient::windowMouseUp(MwWidget handle, void *user,
                                           void *call) {
  (void)handle;
  (void)call;
  static_cast<TCCSystrayClient *>(user)->closeMenu();
}

void TCCSystrayClient::showMenu(SystrayProtocol &protocol,
                                const std::string &id,
                                const std::vector<SystrayMenuEntry> &entries) {
  closeMenu();

  auto icon = std::find_if(
      mIconWidgets.begin(), mIconWidgets.end(), [&](const auto &widget) {
        return widget->protocol == &protocol && widget->id == id;
      });
  if (icon == mIconWidgets.end())
    return;

  mMenu = std::make_unique<OpenMenu>();
  mMenu->protocol = &protocol;
  mMenu->id = id;

  // Sits on the top edge of the bar, above the icons (which start lower), so
  // it never takes their clicks.
  mMenu->holder =
      MwCreateWidget(MwFrameClass, "menuholder", mWindow,
                     MwGetInteger((*icon)->image, MwNx), 0, 1, 1);
  MwShow(mMenu->holder, 0);
  mMenu->menubar =
      MwCreateWidget(MwMenuClass, "menubar", mMenu->holder, 0, 0, 0, 0);
  MwShow(mMenu->menubar, 0);
  // Chosen entries are reported to the menu bar the popup belongs to.
  MwAddUserHandler(mMenu->menubar, MwNmenuHandler, menuChosen, this);

  // The popup shows the entries of a single top level menu.
  MwMenu top = MwMenuAdd(mMenu->menubar, NULL, "");
  addMenuEntries(top, entries);

  MwWidget popup = MwCreateWidget(MwSubMenuClass, "submenu", mMenu->menubar,
                                  0, 0, 0, 0);
  // Opens upwards from the bar.
  MwPoint point = {0, 0};
  MwSubMenuAppear(popup, top, &point, 1);
}

void TCCSystrayClient::addMenuEntries(
    MwMenu parent, const std::vector<SystrayMenuEntry> &entries) {
  for (const auto &entry : entries) {
    if (entry.type == SystrayMenuEntry::Type::Separator) {
      // How MwSubMenu spells a separator.
      MwMenuAdd(mMenu->menubar, parent, "----");
      continue;
    }

    std::string label;
    if (entry.toggle == SystrayMenuEntry::Toggle::Checkmark)
      label = entry.toggled ? "[x] " : "[ ] ";
    else if (entry.toggle == SystrayMenuEntry::Toggle::Radio)
      label = entry.toggled ? "(*) " : "( ) ";
    label += entry.label;

    MwMenu menu = MwMenuAdd(mMenu->menubar, parent, label.c_str());
    if (!entry.children.empty())
      addMenuEntries(menu, entry.children);
    else if (entry.enabled)
      mMenu->entryIds[menu] = entry.id;
  }
}

void TCCSystrayClient::closeMenu() {
  if (!mMenu)
    return;
  // Takes the menu bar, its MwMenu tree and any open popups with it.
  MwDestroyWidget(mMenu->holder);
  mMenu->protocol->menuClosed(mMenu->id);
  mMenu.reset();
}

void MWAPI TCCSystrayClient::menuChosen(MwWidget handle, void *user,
                                        void *call) {
  (void)handle;
  auto *client = static_cast<TCCSystrayClient *>(user);
  if (!client->mMenu)
    return;

  // The popup has already closed itself.
  auto entry = client->mMenu->entryIds.find(static_cast<MwMenu>(call));
  if (entry != client->mMenu->entryIds.end())
    client->mMenu->protocol->menuEntryActivated(client->mMenu->id,
                                                entry->second);
  client->closeMenu();
}

void TCCSystrayClient::run() {
  while (!MwWindowShouldClose(mWindow)) {
    for (auto &protocol : mProtocols)
      protocol->poll();

    if (mNeedsRelayout) {
      mNeedsRelayout = false;
      relayout();
    }

    std::vector<MwPixmap> oldPixmaps = std::move(mOldPixmaps);
    mOldPixmaps.clear();
    MwStep(mWindow);
    for (auto pixmap : oldPixmaps)
      MwDestroyPixmap(pixmap);
  };
}
