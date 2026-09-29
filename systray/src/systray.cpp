#include "systray.hpp"
#ifdef TCC_SYSTRAY_DBUS
#include "dbus/sni_watcher.hpp"
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
#include <sys/wait.h>
#include <unistd.h>

TCCSystrayClient::TCCSystrayClient() {
  mWindow = window_setup(&mBounds);
  // Clicking the bar outside of the icons dismisses an open menu.
  MwAddUserHandler(mWindow, MwNmouseUpHandler, windowMouseUp, this);

  // Moved to the end of the icons by relayout().
  mNotch = MwVaCreateWidget(MwFrameClass, "notch", mWindow, ICON_SPACING, 0,
                            NOTCH_WIDTH, 32, NULL);
  MwAddUserHandler(mNotch, MwNdrawHandler, drawNotch, this);
  MwAddUserHandler(mNotch, MwNmouseUpHandler, notchMouseUp, this);

#ifdef TCC_SYSTRAY_DBUS
  addProtocol(std::make_unique<StatusNotifierWatcher>());
  addProtocol(std::make_unique<UPowerBattery>());
  // The network and Bluetooth items are nm-applet's and blueman-applet's,
  // started once our watcher is registered so they find it.
  launchDetached({"nm-applet", "--indicator"});
  launchDetached({"blueman-applet"});
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

void TCCSystrayClient::launchDetached(std::vector<const char *> argv) {
  argv.push_back(nullptr);
  pid_t pid = fork();
  if (pid < 0) {
    perror("tcc_systray: fork");
    return;
  }
  if (pid == 0) {
    // Fork again so the program is reparented to init and never left as our
    // zombie; it also outlives us in its own session.
    setsid();
    if (fork() == 0) {
      execvp(argv[0], const_cast<char *const *>(argv.data()));
      fprintf(stderr, "tcc_systray: could not launch %s\n", argv[0]);
      _exit(127);
    }
    _exit(0);
  }
  waitpid(pid, nullptr, 0);
}

void TCCSystrayClient::addProtocol(std::unique_ptr<SystrayProtocol> protocol) {
  protocol->setIconSize(ICON_SIZE);
  protocol->setShowMenuCallback(
      [this](SystrayProtocol &protocol, const std::string &id,
             const std::vector<SystrayMenuEntry> &entries) {
        showMenu(protocol, id, entries);
      });
  protocol->setShowMixerCallback(
      [this](SystrayProtocol &protocol, const std::string &id,
             const SystrayMixer &mixer) { showMixer(protocol, id, mixer); });
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
      mIcons.get_icon_by_name(name.c_str(), icon.themePath.c_str(), size, width,
                              height, &pixels);
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
      loadIcon(overlay, std::max(1, std::min(width, height) / 2), &overlayWidth,
               &overlayHeight);
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
  // Don't leave a menu or mixer open for an item that went away.
  auto gone = [](SystrayProtocol *protocol, const std::string &id) {
    const auto &items = protocol->items();
    return std::none_of(items.begin(), items.end(),
                        [&](const SystrayItem &item) { return item.id == id; });
  };
  if (mMenu && gone(mMenu->protocol, mMenu->id))
    closeMenu();
  if (mMixer && gone(mMixer->protocol, mMixer->id))
    closeMixer();

  for (auto &widget : mIconWidgets) {
    MwDestroyWidget(widget->image);
    if (widget->pixmap)
      mOldPixmaps.push_back(widget->pixmap);
  }
  mIconWidgets.clear();

  int x = ICON_SPACING;
  for (auto &protocol : mProtocols) {
    // Collapsed down to just the notch.
    if (mCollapsed)
      break;
    for (const auto &item : protocol->items()) {
      if (item.status == SystrayItem::Status::Passive)
        continue;

      // A text icon, or an image drawn at its own size rather than stretched
      // to fill the button.
      MwPixmap pixmap = nullptr;
      if (item.currentIcon().text.empty()) {
        int width, height;
        unsigned char *pixels =
            loadIcon(item.currentIcon(), ICON_SIZE, &width, &height);
        if (!pixels)
          mIcons.get_icon_by_name("application-x-executable", ICON_SIZE, &width,
                                  &height, &pixels);
        if (!pixels)
          continue;
        drawOverlay(item.overlayIcon, pixels, width, height);
        pixmap = MwLoadRaw(mWindow, pixels, width, height);
        free(pixels);
      }

      MwWidget button = MwVaCreateWidget(MwButtonClass, NULL, mWindow, x,
                                         (32 - BUTTON_SIZE) / 2, BUTTON_SIZE,
                                         BUTTON_SIZE, MwNfillArea, 0, NULL);
      if (pixmap)
        MwVaApply(button, MwNpixmap, pixmap, NULL);
      else
        MwVaApply(button, MwNtext, item.currentIcon().text.c_str(), NULL);

      auto widget = std::make_unique<IconWidget>(
          IconWidget{this, button, pixmap, protocol.get(), item.id});
      MwAddUserHandler(button, MwNmouseDownHandler, iconMouseDown,
                       widget.get());
      MwAddUserHandler(button, MwNmouseUpHandler, iconMouseUp, widget.get());
      mIconWidgets.push_back(std::move(widget));
      x += BUTTON_SIZE;
    }
  }

  MwVaApply(mNotch, MwNx, x, NULL);
  MwVaApply(mWindow, MwNwidth,
            (mIconWidgets.size() * BUTTON_SIZE) + ICON_SPACING + NOTCH_WIDTH,
            MwNheight, 32, NULL);
}

// Draws the notch over the frame's plain background: a rounded tab with a
// gradient like a button's, and a grip of vertical ridges
void MWAPI TCCSystrayClient::drawNotch(MwWidget handle, void *user,
                                       void *call) {
  (void)user;
  (void)call;
  int w = MwGetInteger(handle, MwNwidth);
  int h = MwGetInteger(handle, MwNheight);

  MwColor base = MwParseColor(handle, MwGetString(handle, MwNbackground));
  MwColor light = MwLightenColor(handle, base, 64, 64, 64);
  MwColor dark = MwLightenColor(handle, base, -96, -96, -96);

  auto rect = [&](int x, int y, int width, int height, MwColor color) {
    MwRect r = {x, y, width, height};
    MwDrawRect(handle, &r, color);
  };

  MwRect body = {0, 0, w, h};
  MwDrawRectFading(handle, &body, base);

  // Bevelled like a raised button: lit from the top left.
  rect(0, 0, w, 1, light);
  rect(0, 0, 1, h, light);
  rect(0, h - 1, w, 1, dark);
  rect(w - 1, 0, 1, h, dark);

  // The grip: three ridges, each a shadow with a highlight beside it.
  int gripTop = 8;
  int gripHeight = h - gripTop * 2;
  int gripLeft = (w - 8) / 2;
  for (int i = 0; i < 3; i++) {
    rect(gripLeft + i * 3, gripTop, 1, gripHeight, dark);
    rect(gripLeft + i * 3 + 1, gripTop, 1, gripHeight, light);
  }

  MwFreeColor(dark);
  MwFreeColor(light);
  MwFreeColor(base);
}

void MWAPI TCCSystrayClient::notchMouseUp(MwWidget handle, void *user,
                                          void *call) {
  (void)handle;
  auto *client = static_cast<TCCSystrayClient *>(user);
  if (static_cast<MwMouse *>(call)->button != MwMOUSE_LEFT)
    return;

  // Popups belong to icons that are about to go away.
  client->closePopups(nullptr, {});
  client->mCollapsed = !client->mCollapsed;
  // Not right away, we're inside one of Milsko's handlers.
  client->mNeedsRelayout = true;
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

  // Any click on the tray dismisses an open menu or mixer, and clicking the
  // item it belongs to toggles it off rather than opening it again.
  if (client->closePopups(icon.protocol, icon.id) &&
      mouse.button != MwMOUSE_MIDDLE)
    return;

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
  static_cast<TCCSystrayClient *>(user)->closePopups(nullptr, {});
}

bool TCCSystrayClient::closePopups(SystrayProtocol *protocol,
                                   const std::string &id) {
  bool sameItem = (mMenu && mMenu->protocol == protocol && mMenu->id == id) ||
                  (mMixer && mMixer->protocol == protocol && mMixer->id == id);
  closeMenu();
  closeMixer();
  return sameItem;
}

void TCCSystrayClient::showMixer(SystrayProtocol &protocol,
                                 const std::string &id,
                                 const SystrayMixer &mixer) {
  // The protocol keeps an open mixer up to date through here too.
  if (mMixer && mMixer->protocol == &protocol && mMixer->id == id) {
    mMixer->window->update(mixer);
    return;
  }
  closePopups(nullptr, {});

  auto icon = std::find_if(
      mIconWidgets.begin(), mIconWidgets.end(), [&](const auto &widget) {
        return widget->protocol == &protocol && widget->id == id;
      });
  if (icon == mIconWidgets.end())
    return;

  auto *target = &protocol;
  std::string item = id;
  mMixer = std::make_unique<OpenMixer>();
  mMixer->protocol = &protocol;
  mMixer->id = id;
  mMixer->window = std::make_unique<MixerWindow>(
      mWindow, MwGetInteger((*icon)->image, MwNx), mixer,
      [target, item](int channel, int volume) {
        target->mixerVolumeChanged(item, channel, volume);
      },
      [target, item](int channel, bool muted) {
        target->mixerMuteChanged(item, channel, muted);
      });
}

void TCCSystrayClient::closeMixer() {
  if (!mMixer)
    return;
  mMixer->protocol->mixerClosed(mMixer->id);
  mMixer.reset();
}

void TCCSystrayClient::showMenu(SystrayProtocol &protocol,
                                const std::string &id,
                                const std::vector<SystrayMenuEntry> &entries) {
  closePopups(nullptr, {});

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
  mMenu->holder = MwCreateWidget(MwFrameClass, "menuholder", mWindow,
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

  MwWidget popup =
      MwCreateWidget(MwSubMenuClass, "submenu", mMenu->menubar, 0, 0, 0, 0);
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

    bool relaidOut = mNeedsRelayout;
    if (mNeedsRelayout) {
      mNeedsRelayout = false;
      relayout();
    }

    std::vector<MwPixmap> oldPixmaps = std::move(mOldPixmaps);
    mOldPixmaps.clear();
    MwStep(mWindow);
    for (auto pixmap : oldPixmaps)
      MwDestroyPixmap(pixmap);

    // Removed icons are only gone once that step has freed them, and nothing
    // repaints where they were otherwise (e.g. what collapsing leaves under
    // the notch).
    if (relaidOut)
      MwForceRender(mWindow);
  };
}
