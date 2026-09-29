#ifdef TCC_HAS_DBUS
#pragma once

#include "dbus_loader.hpp"

#include <functional>
#include <vector>

// A private, non-blocking connection to a message bus, for protocols that only
// talk to services on it (as opposed to StatusNotifierWatcher, which also
// provides one).
class DBusClient {
public:
  // Returns true to stop other handlers from seeing the message.
  using MessageHandler = std::function<bool(DBusMessage *msg)>;
  using ReplyHandler = std::function<void(DBusMessage *reply)>;
  // Called for each entry of an a{sv} dictionary.
  using PropertyHandler =
      std::function<void(const char *name, DBusMessageIter *value)>;

  DBusClient() = default;
  ~DBusClient();
  DBusClient(const DBusClient &) = delete;
  DBusClient &operator=(const DBusClient &) = delete;

  // Connects to the given bus. Returns false if libdbus or the bus is
  // unavailable.
  bool open(DBusBusType type);
  void close();
  bool isOpen() const { return mConn != nullptr; }

  // Processes pending traffic without blocking. Closes the connection if it
  // was lost.
  void poll();
  int fd() const;

  DBusConnection *connection() const { return mConn; }

  // Adds a match rule for signals to receive.
  void addMatch(const char *rule);
  // Handles every incoming message (signals, mostly).
  void setMessageHandler(MessageHandler handler) {
    mMessageHandler = std::move(handler);
  }

  // Sends msg, then runs onReply with the reply. The reply is an error message
  // if the call failed, or null if it timed out or the connection closed.
  void callAsync(DBusMessage *msg, ReplyHandler onReply, int timeoutMs = 5000);
  // Properties.GetAll(iface) on an object, with the a{sv} reply read by
  // forEachProperty(). onDone(false) if the call failed.
  void getAllProperties(const char *service, const char *path,
                        const char *iface, PropertyHandler onProperty,
                        std::function<void(bool ok)> onDone);
  // Iterates an a{sv} dictionary.
  void forEachProperty(DBusMessageIter *dict, const PropertyHandler &handler);

  static bool isError(DBusMessage *reply) {
    return !reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR;
  }

private:
  struct Call {
    DBusClient *self;
    ReplyHandler onReply;
  };

  DBusConnection *mConn = nullptr;
  MessageHandler mMessageHandler;
  // Outstanding calls, cancelled on close.
  std::vector<DBusPendingCall *> mCalls;

  static DBusHandlerResult filterThunk(DBusConnection *, DBusMessage *msg,
                                       void *self);
  static void replyThunk(DBusPendingCall *pending, void *data);
};
#endif
