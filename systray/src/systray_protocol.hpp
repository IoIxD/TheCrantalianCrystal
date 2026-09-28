#pragma once

#include <functional>
#include <string>
#include <vector>

// A source of systray items, e.g. StatusNotifierItem over D-Bus or the X11
// XEmbed system tray protocol.
//
// Each protocol identifies its items with strings of its own choosing; they
// are only unique within that protocol.
class SystrayProtocol {
public:
  using ItemsChangedCallback = std::function<void(
      SystrayProtocol &protocol, const std::vector<std::string> &items)>;

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

  const std::vector<std::string> &items() const { return mItems; }
  void setItemsChangedCallback(ItemsChangedCallback cb) {
    mItemsChanged = std::move(cb);
  }

protected:
  std::vector<std::string> mItems;

  // Implementations call this after modifying mItems.
  void itemsChanged() {
    if (mItemsChanged)
      mItemsChanged(*this, mItems);
  }

private:
  ItemsChangedCallback mItemsChanged;
};
