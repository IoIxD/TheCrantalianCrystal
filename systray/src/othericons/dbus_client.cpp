#ifdef TCC_SYSTRAY_DBUS
#include "dbus_client.hpp"

#include <cstdio>

DBusClient::~DBusClient() { close(); }

bool DBusClient::open(DBusBusType type) {
  if (mConn)
    return true;

  mLib = DBusLib::get();
  if (!mLib)
    return false;

  DBusError err;
  mLib->dbus_error_init(&err);
  mConn = mLib->dbus_bus_get_private(type, &err);
  if (!mConn) {
    fprintf(stderr, "tcc_systray: could not connect to the %s bus: %s\n",
            type == DBUS_BUS_SYSTEM ? "system" : "session",
            mLib->dbus_error_is_set(&err) ? err.message : "unknown error");
    mLib->dbus_error_free(&err);
    return false;
  }
  mLib->dbus_connection_set_exit_on_disconnect(mConn, false);
  mLib->dbus_connection_add_filter(mConn, filterThunk, this, nullptr);
  return true;
}

void DBusClient::close() {
  if (!mConn)
    return;
  for (DBusPendingCall *pending : mCalls) {
    mLib->dbus_pending_call_cancel(pending);
    mLib->dbus_pending_call_unref(pending);
  }
  mCalls.clear();
  mLib->dbus_connection_close(mConn);
  mLib->dbus_connection_unref(mConn);
  mConn = nullptr;
}

void DBusClient::poll() {
  if (!mConn)
    return;

  if (!mLib->dbus_connection_read_write(mConn, 0)) {
    fprintf(stderr, "tcc_systray: lost connection to a message bus\n");
    close();
    return;
  }
  while (mConn &&
         mLib->dbus_connection_dispatch(mConn) == DBUS_DISPATCH_DATA_REMAINS) {
  }
}

int DBusClient::fd() const {
  int fd = -1;
  if (mConn)
    mLib->dbus_connection_get_unix_fd(mConn, &fd);
  return fd;
}

void DBusClient::addMatch(const char *rule) {
  if (mConn)
    mLib->dbus_bus_add_match(mConn, rule, nullptr);
}

DBusHandlerResult DBusClient::filterThunk(DBusConnection *, DBusMessage *msg,
                                          void *self) {
  auto *client = static_cast<DBusClient *>(self);
  if (client->mMessageHandler && client->mMessageHandler(msg))
    return DBUS_HANDLER_RESULT_HANDLED;
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void DBusClient::callAsync(DBusMessage *msg, ReplyHandler onReply,
                           int timeoutMs) {
  if (!mConn) {
    onReply(nullptr);
    return;
  }
  DBusPendingCall *pending = nullptr;
  if (!mLib->dbus_connection_send_with_reply(mConn, msg, &pending, timeoutMs) ||
      !pending) {
    onReply(nullptr);
    return;
  }
  mCalls.push_back(pending);
  mLib->dbus_pending_call_set_notify(
      pending, replyThunk, new Call{this, std::move(onReply)},
      [](void *data) { delete static_cast<Call *>(data); });
}

void DBusClient::replyThunk(DBusPendingCall *pending, void *data) {
  // Copied out, since releasing the pending call frees data.
  Call call = *static_cast<Call *>(data);
  DBusLib *d = call.self->mLib;

  DBusMessage *reply = d->dbus_pending_call_steal_reply(pending);
  std::erase(call.self->mCalls, pending);
  d->dbus_pending_call_unref(pending);

  call.onReply(reply);
  if (reply)
    d->dbus_message_unref(reply);
}

void DBusClient::getAllProperties(const char *service, const char *path,
                                  const char *iface, PropertyHandler onProperty,
                                  std::function<void(bool ok)> onDone) {
  if (!mConn) {
    onDone(false);
    return;
  }
  DBusLib *d = mLib;

  DBusMessage *msg = mLib->dbus_message_new_method_call(
      service, path, "org.freedesktop.DBus.Properties", "GetAll");
  mLib->dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
                                 DBUS_TYPE_INVALID);
  callAsync(msg, [this, d, onProperty, onDone](DBusMessage *reply) {
    DBusMessageIter iter;
    if (isError(d, reply) || !mLib->dbus_message_iter_init(reply, &iter)) {
      onDone(false);
      return;
    }
    forEachProperty(&iter, onProperty);
    onDone(true);
  });
  mLib->dbus_message_unref(msg);
}

void DBusClient::forEachProperty(DBusMessageIter *dict,
                                 const PropertyHandler &handler) {

  DBusMessageIter entries, entry, value;
  if (mLib->dbus_message_iter_get_arg_type(dict) != DBUS_TYPE_ARRAY)
    return;

  mLib->dbus_message_iter_recurse(dict, &entries);
  while (mLib->dbus_message_iter_get_arg_type(&entries) ==
         DBUS_TYPE_DICT_ENTRY) {
    const char *name;
    mLib->dbus_message_iter_recurse(&entries, &entry);
    mLib->dbus_message_iter_get_basic(&entry, &name);
    mLib->dbus_message_iter_next(&entry);
    mLib->dbus_message_iter_recurse(&entry, &value);
    handler(name, &value);
    mLib->dbus_message_iter_next(&entries);
  }
}
#endif
