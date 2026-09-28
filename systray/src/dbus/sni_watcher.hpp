#ifdef TCC_SYSTRAY_DBUS
#pragma once

#include "../systray_protocol.hpp"
#include "dbus_loader.hpp"

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

  static DBusHandlerResult filterThunk(DBusConnection *, DBusMessage *msg,
                                       void *self);
  DBusHandlerResult filter(DBusMessage *msg);

  void disconnect();
  void becomeWatcher();
  void becomeHost();
  void refreshFromWatcher();

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
