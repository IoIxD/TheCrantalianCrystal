#ifdef TCC_SYSTRAY_DBUS
#pragma once

#include "../systray_protocol.hpp"
#include "../lib/dbus_loader.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

// Tracks the StatusNotifierItems (systray icons) on the session bus.
//
// If nobody owns org.kde.StatusNotifierWatcher we take the name and act as the
// watcher ourselves. Otherwise we register as a StatusNotifierHost with the
// existing watcher and mirror its item list. We stay queued for the watcher
// name, so if the existing watcher exits we take over.
//
// Items are identified the same way the KDE watcher does it: the bus name of
// the item followed by its object path, e.g. ":1.42/StatusNotifierItem".
// Their title, status and icons are fetched from the item itself, and fetched
// again whenever the item signals that they changed.
class StatusNotifierWatcher : public SystrayProtocol {
public:
  enum class Mode { Disconnected, Watcher, Host };

  StatusNotifierWatcher() = default;
  ~StatusNotifierWatcher() override;

  const char *name() const override { return "StatusNotifierItem"; }
  // Connects to the session bus and either creates or attaches to the watcher.
  // Returns false if libdbus or the session bus is unavailable.
  bool connect() override;
  // Processes pending D-Bus traffic without blocking.
  void poll() override;
  // File descriptor of the bus connection.
  int fd() const override;

  void activate(const std::string &id, int x, int y) override;
  void secondaryActivate(const std::string &id, int x, int y) override;
  void contextMenu(const std::string &id, int x, int y) override;
  void scroll(const std::string &id, int delta, bool horizontal) override;
  void menuEntryActivated(const std::string &id, int entryId) override;
  void menuClosed(const std::string &id) override;

  Mode mode() const { return mMode; }

private:
  DBusLib *mLib = nullptr;
  DBusConnection *mConn = nullptr;
  Mode mMode = Mode::Disconnected;
  std::string mUniqueName;
  std::string mHostName;
  std::vector<std::string> mHosts; // hosts registered with us (watcher mode)
  bool mNeedsRefresh = false;
  DBusError mDBusErr;

  // The D-Bus side of each item in mItems, keyed by item id.
  struct ItemConn {
    std::string service;
    std::string path;
    // Unique name of the item's connection, which its signals come from.
    std::string owner;
    // Outstanding property fetch, if any.
    DBusPendingCall *pending = nullptr;
    // Object path of the item's com.canonical.dbusmenu menu, if it has one.
    std::string menuPath;
  };
  std::map<std::string, ItemConn> mItemConns;

  // Outstanding method calls on items, cancelled on disconnect.
  struct ItemCall;
  std::vector<DBusPendingCall *> mItemCalls;

  static DBusHandlerResult filterThunk(DBusConnection *, DBusMessage *msg,
                                       void *self);
  DBusHandlerResult filter(DBusMessage *msg);

  void disconnect();
  void becomeWatcher();
  void becomeHost();
  void refreshFromWatcher();

  SystrayItem *findItem(const std::string &id);
  // These don't call itemsChanged(); addItem() calls it once the item's
  // properties arrive.
  void addItem(const std::string &id);
  void removeItem(const std::string &id);
  void clearItems();

  void fetchItemProperties(const std::string &id);
  static void propertiesReplyThunk(DBusPendingCall *pending, void *data);
  void propertiesReply(const std::string &id, DBusMessage *reply);
  void readItemProperty(SystrayItem &item, const char *prop,
                        DBusMessageIter *value);
  void readPixmaps(SystrayIcon &icon, DBusMessageIter *value);
  bool handleItemSignal(DBusMessage *msg);

  // Sends msg without blocking, then runs onReply with the reply. The reply is
  // an error message if the call failed, or null if it timed out.
  void callAsync(DBusMessage *msg,
                 std::function<void(DBusMessage *reply)> onReply);
  static void itemCallReplyThunk(DBusPendingCall *pending, void *data);
  // Calls an (x, y) method such as Activate on an item. onError, if given, is
  // run if the call fails (e.g. the item doesn't implement it).
  void callItemMethod(const std::string &id, const char *method, int x, int y,
                      std::function<void()> onError = nullptr);

  // com.canonical.dbusmenu menus.
  void fetchMenu(const std::string &id);
  void readMenuLayout(DBusMessageIter *layout, SystrayMenuEntry &entry,
                      bool *visible);
  void sendMenuEvent(const std::string &id, int entryId, const char *event);

  void handleNameOwnerChanged(const char *name, const char *oldOwner,
                              const char *newOwner);
  DBusHandlerResult handleWatcherCall(DBusMessage *msg);
  void registerItem(DBusMessage *msg);
  void registerHost(DBusMessage *msg);
  void getProperty(DBusMessage *msg);
  void getAllProperties(DBusMessage *msg);
  void introspect(DBusMessage *msg);

  void appendPropertyValue(DBusMessageIter *iter, const std::string &prop);
  void emitSignal(const char *member, const std::string &arg);
  void sendReply(DBusMessage *reply);
};
#endif
