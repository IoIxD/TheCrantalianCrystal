#pragma once

#include <functional>
#include <string>
#include <vector>

// How an item describes one of its icons. A protocol fills in whichever of
// these it has; the client prefers the name and falls back to the pixels.
struct SystrayIcon {
  // Icon theme name, or an absolute path to an image file.
  std::string name;
  // Extra directory to look for name in before the icon theme.
  std::string themePath;
  // Tightly packed 8-bit RGBA.
  int width = 0, height = 0;
  std::vector<unsigned char> pixels;

  bool empty() const { return name.empty() && pixels.empty(); }
  bool operator==(const SystrayIcon &) const = default;
};

struct SystrayItem {
  enum class Status { Active, Passive, NeedsAttention };

  // Unique within the protocol that reported the item.
  std::string id;
  std::string title;
  Status status = Status::Active;
  // Clicking the item should show its menu rather than activate it.
  bool itemIsMenu = false;
  SystrayIcon icon;
  SystrayIcon attentionIcon;
  // Small emblem to draw over the icon.
  SystrayIcon overlayIcon;

  // The icon that should be shown right now.
  const SystrayIcon &currentIcon() const {
    if (status == Status::NeedsAttention && !attentionIcon.empty())
      return attentionIcon;
    return icon;
  }
  bool operator==(const SystrayItem &) const = default;
};

// A source of systray items, e.g. StatusNotifierItem over D-Bus or the X11
// XEmbed system tray protocol.
class SystrayProtocol {
public:
  using ItemsChangedCallback = std::function<void(
      SystrayProtocol &protocol, const std::vector<SystrayItem> &items)>;

  SystrayProtocol() = default;
  virtual ~SystrayProtocol() = default;
  SystrayProtocol(const SystrayProtocol &) = delete;
  SystrayProtocol &operator=(const SystrayProtocol &) = delete;

  // Human readable name of the protocol, for logging.
  virtual const char *name() const = 0;
  // Sets up the protocol. Returns false if it is unavailable.
  virtual bool connect() = 0;
  // Processes pending events without blocking.
  virtual void poll() = 0;
  // File descriptor to wait on for events (for poll()/select()), or -1.
  virtual int fd() const { return -1; }

  // Mouse interaction with an item. x and y are the screen position of the
  // click, which the item may use to place a window or menu.
  // Primary action, usually a left click.
  virtual void activate(const std::string &id, int x, int y) {}
  // Usually a middle click.
  virtual void secondaryActivate(const std::string &id, int x, int y) {}
  // Usually a right click.
  virtual void contextMenu(const std::string &id, int x, int y) {}
  // delta is in wheel steps, positive is up or right.
  virtual void scroll(const std::string &id, int delta, bool horizontal) {}

  const std::vector<SystrayItem> &items() const { return mItems; }
  void setItemsChangedCallback(ItemsChangedCallback cb) {
    mItemsChanged = std::move(cb);
  }
  // Size icons will be drawn at, used to pick between pixmaps of several
  // sizes.
  void setIconSize(int size) { mIconSize = size; }

protected:
  std::vector<SystrayItem> mItems;
  int mIconSize = 22;

  // Implementations call this after modifying mItems.
  void itemsChanged() {
    if (mItemsChanged)
      mItemsChanged(*this, mItems);
  }

private:
  ItemsChangedCallback mItemsChanged;
};
