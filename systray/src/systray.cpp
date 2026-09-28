#include "systray.hpp"
#ifdef TCC_SYSTRAY_DBUS
#include "dbus/sni_watcher.hpp"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>

TCCSystrayClient::TCCSystrayClient() {
  mWindow = window_setup(&mBounds);

#ifdef TCC_SYSTRAY_DBUS
  addProtocol(std::make_unique<StatusNotifierWatcher>());
#endif

  if (mProtocols.empty())
    fprintf(stderr, "tcc_systray: systray icons will not be available\n");
}

void TCCSystrayClient::addProtocol(std::unique_ptr<SystrayProtocol> protocol) {
  protocol->setIconSize(ICON_SIZE);
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
  for (auto &widget : mIconWidgets) {
    MwDestroyWidget(widget->image);
    mOldPixmaps.push_back(widget->pixmap);
  }
  mIconWidgets.clear();

  int x = ICON_SPACING;
  for (auto &protocol : mProtocols) {
    for (const auto &item : protocol->items()) {
      if (item.status == SystrayItem::Status::Passive)
        continue;

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

  int x, y;
  icon.client->screenPosition(icon, mouse, &x, &y);
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
