#ifdef TCC_SYSTRAY_DBUS
#include "sni_watcher.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <unistd.h>

constexpr const char *WATCHER_NAME = "org.kde.StatusNotifierWatcher";
constexpr const char *WATCHER_IFACE = "org.kde.StatusNotifierWatcher";
constexpr const char *WATCHER_PATH = "/StatusNotifierWatcher";
constexpr const char *HOST_NAME_PREFIX = "org.kde.StatusNotifierHost-";
constexpr const char *ITEM_DEFAULT_PATH = "/StatusNotifierItem";
constexpr const char *PROPS_IFACE = "org.freedesktop.DBus.Properties";
constexpr const char *INTROSPECT_IFACE = "org.freedesktop.DBus.Introspectable";

constexpr const char *INTROSPECT_XML = DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE
    R"TCC_XML(<node>
  <interface name=\"org.freedesktop.DBus.Introspectable\">
    <method name=\"Introspect\">
      <arg name=\"xml_data\" type=\"s\" direction=\"out\"/>
    </method>
  </interface>
  <interface name=\"org.freedesktop.DBus.Properties\">
    <method name=\"Get\">
      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>
      <arg name=\"property_name\" type=\"s\" direction=\"in\"/>
      <arg name=\"value\" type=\"v\" direction=\"out\"/>
    </method>
    <method name=\"GetAll\">
      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>
      <arg name=\"properties\" type=\"a{sv}\" direction=\"out\"/>
    </method>
  </interface>
  <interface name=\"org.kde.StatusNotifierWatcher\">
    <method name=\"RegisterStatusNotifierItem\">
      <arg name=\"service\" type=\"s\" direction=\"in\"/>
    </method>
    <method name=\"RegisterStatusNotifierHost\">
      <arg name=\"service\" type=\"s\" direction=\"in\"/>
    </method>
    <property name=\"RegisteredStatusNotifierItems\" type=\"as\" "
access=\"read\"/>
    <property name=\"IsStatusNotifierHostRegistered\" type=\"b\" "
access=\"read\"/>
    <property name=\"ProtocolVersion\" type=\"i\" access=\"read\"/>
    <signal name=\"StatusNotifierItemRegistered\">
      <arg type=\"s\"/>
    </signal>
    <signal name=\"StatusNotifierItemUnregistered\">
      <arg type=\"s\"/>
    </signal>
    <signal name=\"StatusNotifierHostRegistered\"/>
    <signal name=\"StatusNotifierHostUnregistered\"/>
  </interface>
</node>)TCC_XML";

bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

// The bus name part of an item id ("<service>/<path>").
std::string itemService(const std::string &item) {
  return item.substr(0, item.find('/'));
}

StatusNotifierWatcher::~StatusNotifierWatcher() {
  setItemsChangedCallback(nullptr);
  disconnect();
}

bool StatusNotifierWatcher::connect() {
  if (mConn)
    return true;

  mLib = DBusLib::get();
  if (!mLib)
    return false;
  DBusLib *d = mLib;

  d->dbus_error_init(&mDBusErr);

  mConn = d->dbus_bus_get_private(DBUS_BUS_SESSION, &mDBusErr);
  if (!mConn) {
    fprintf(stderr, "tcc_systray: could not connect to session bus: %s\n",
            d->dbus_error_is_set(&mDBusErr) ? mDBusErr.message
                                            : "unknown error");
    return false;
  }
  d->dbus_connection_set_exit_on_disconnect(mConn, false);
  mUniqueName = d->dbus_bus_get_unique_name(mConn);

  d->dbus_connection_add_filter(mConn, filterThunk, this, nullptr);
  d->dbus_bus_add_match(mConn,
                        "type='signal',sender='org.freedesktop.DBus',"
                        "interface='org.freedesktop.DBus',"
                        "member='NameOwnerChanged'",
                        nullptr);
  d->dbus_bus_add_match(
      mConn, "type='signal',interface='org.kde.StatusNotifierWatcher'",
      nullptr);

  // Every host owns a name of this form so that the watcher (and items) can
  // tell when it goes away.
  mHostName = HOST_NAME_PREFIX + std::to_string(getpid());
  d->dbus_bus_request_name(mConn, mHostName.c_str(),
                           DBUS_NAME_FLAG_DO_NOT_QUEUE, nullptr);

  // No DO_NOT_QUEUE: if someone else is the watcher we wait in line and the
  // bus hands us the name (via NameAcquired) once they exit.
  int ret = d->dbus_bus_request_name(mConn, WATCHER_NAME, 0, &mDBusErr);
  if (d->dbus_error_is_set(&mDBusErr)) {
    fprintf(stderr, "tcc_systray: could not request %s: %s\n", WATCHER_NAME,
            mDBusErr.message);
    disconnect();
    return false;
  }

  if (ret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
      ret == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER)
    becomeWatcher();
  else
    becomeHost();

  return true;
}

void StatusNotifierWatcher::disconnect() {
  if (!mConn)
    return;
  mLib->dbus_connection_close(mConn);
  mLib->dbus_connection_unref(mConn);
  mConn = nullptr;
  mMode = Mode::Disconnected;
  mHosts.clear();
  if (!mItems.empty()) {
    mItems.clear();
    itemsChanged();
  }
}

int StatusNotifierWatcher::fd() const {
  int fd = -1;
  if (mConn)
    mLib->dbus_connection_get_unix_fd(mConn, &fd);
  return fd;
}

void StatusNotifierWatcher::poll() {
  if (!mConn)
    return;
  DBusLib *d;
  if ((d = mLib) == nullptr)
    return;

  if (!d->dbus_connection_read_write(mConn, 0)) {
    fprintf(stderr, "tcc_systray: lost connection to session bus\n");
    disconnect();
    return;
  }
  while (d->dbus_connection_dispatch(mConn) == DBUS_DISPATCH_DATA_REMAINS) {
  }

  // Done outside of dispatch since it makes a blocking call.
  if (mNeedsRefresh) {
    mNeedsRefresh = false;
    refreshFromWatcher();
  }
}

void StatusNotifierWatcher::becomeWatcher() {
  if (mMode == Mode::Watcher)
    return;
  mMode = Mode::Watcher;
  mNeedsRefresh = false;

  // Items registered with the previous watcher will re-register with us once
  // they see the owner of the watcher name change.
  mHosts.clear();
  if (!mItems.empty()) {
    mItems.clear();
    itemsChanged();
  }
  emitSignal("StatusNotifierHostRegistered", {});
}

void StatusNotifierWatcher::becomeHost() {
  mMode = Mode::Host;
  mHosts.clear();
  // Fetched on the next poll(), since this may be called during dispatch.
  mNeedsRefresh = true;
}

void StatusNotifierWatcher::refreshFromWatcher() {
  if (mMode != Mode::Host)
    return;
  DBusLib *d;
  if ((d = mLib) == nullptr)
    return;

  DBusMessage *msg = d->dbus_message_new_method_call(
      WATCHER_NAME, WATCHER_PATH, WATCHER_IFACE, "RegisterStatusNotifierHost");
  const char *host = mHostName.c_str();
  d->dbus_message_append_args(msg, DBUS_TYPE_STRING, &host, DBUS_TYPE_INVALID);
  d->dbus_message_set_no_reply(msg, true);
  d->dbus_connection_send(mConn, msg, nullptr);
  d->dbus_message_unref(msg);

  msg = d->dbus_message_new_method_call(WATCHER_NAME, WATCHER_PATH, PROPS_IFACE,
                                        "Get");
  const char *iface = WATCHER_IFACE;
  const char *prop = "RegisteredStatusNotifierItems";
  d->dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING,
                              &prop, DBUS_TYPE_INVALID);

  DBusMessage *reply =
      d->dbus_connection_send_with_reply_and_block(mConn, msg, 1000, &mDBusErr);
  d->dbus_message_unref(msg);

  std::vector<std::string> items;
  if (!reply) {
    fprintf(stderr, "tcc_systray: could not get items from %s: %s\n",
            WATCHER_NAME, mDBusErr.message);
  } else {
    DBusMessageIter iter, variant, array;
    if (d->dbus_message_iter_init(reply, &iter) &&
        d->dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
      d->dbus_message_iter_recurse(&iter, &variant);
      if (d->dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
        d->dbus_message_iter_recurse(&variant, &array);
        while (d->dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
          const char *item;
          d->dbus_message_iter_get_basic(&array, &item);
          items.emplace_back(item);
          d->dbus_message_iter_next(&array);
        }
      }
    }
    d->dbus_message_unref(reply);
  }

  if (items != mItems) {
    mItems = std::move(items);
    itemsChanged();
  }
}

DBusHandlerResult StatusNotifierWatcher::filterThunk(DBusConnection *,
                                                     DBusMessage *msg,
                                                     void *self) {
  return static_cast<StatusNotifierWatcher *>(self)->filter(msg);
}

DBusHandlerResult StatusNotifierWatcher::filter(DBusMessage *msg) {
  DBusLib *d;
  if ((d = mLib) == nullptr)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  const char *arg = nullptr;

  if (d->dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
    const char *name, *oldOwner, *newOwner;
    if (d->dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &name,
                                 DBUS_TYPE_STRING, &oldOwner, DBUS_TYPE_STRING,
                                 &newOwner, DBUS_TYPE_INVALID))
      handleNameOwnerChanged(name, oldOwner, newOwner);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (d->dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameAcquired")) {
    if (d->dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                                 DBUS_TYPE_INVALID) &&
        eq(arg, WATCHER_NAME))
      becomeWatcher();
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (d->dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameLost")) {
    if (d->dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                                 DBUS_TYPE_INVALID) &&
        eq(arg, WATCHER_NAME) && mMode == Mode::Watcher)
      becomeHost();
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  // While we are the watcher our own signals are echoed back to us; ignore
  // them.
  if (mMode == Mode::Host) {
    bool registered = d->dbus_message_is_signal(msg, WATCHER_IFACE,
                                                "StatusNotifierItemRegistered");
    bool unregistered = d->dbus_message_is_signal(
        msg, WATCHER_IFACE, "StatusNotifierItemUnregistered");
    if ((registered || unregistered) &&
        d->dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                                 DBUS_TYPE_INVALID)) {
      auto it = std::find(mItems.begin(), mItems.end(), arg);
      if (registered && it == mItems.end()) {
        mItems.emplace_back(arg);
        itemsChanged();
      } else if (unregistered && it != mItems.end()) {
        mItems.erase(it);
        itemsChanged();
      }
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }

  if (mMode == Mode::Watcher &&
      d->dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL &&
      eq(d->dbus_message_get_path(msg), WATCHER_PATH))
    return handleWatcherCall(msg);

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void StatusNotifierWatcher::handleNameOwnerChanged(const char *name,
                                                   const char *oldOwner,
                                                   const char *newOwner) {
  (void)oldOwner;
  bool vanished = !newOwner || !*newOwner;

  if (mMode == Mode::Host) {
    if (!eq(name, WATCHER_NAME))
      return;
    if (vanished) {
      // We are queued for the name, so NameAcquired should follow shortly.
      if (!mItems.empty()) {
        mItems.clear();
        itemsChanged();
      }
    } else if (mUniqueName != newOwner) {
      // Some other process became the watcher; re-register with it.
      mNeedsRefresh = true;
    }
    return;
  }

  if (mMode != Mode::Watcher || !vanished)
    return;

  bool changed = false;
  for (auto it = mItems.begin(); it != mItems.end();) {
    if (itemService(*it) == name) {
      emitSignal("StatusNotifierItemUnregistered", *it);
      it = mItems.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (changed)
    itemsChanged();

  auto host = std::find(mHosts.begin(), mHosts.end(), name);
  if (host != mHosts.end()) {
    mHosts.erase(host);
    emitSignal("StatusNotifierHostUnregistered", {});
  }
}

DBusHandlerResult StatusNotifierWatcher::handleWatcherCall(DBusMessage *msg) {
  const char *iface = mLib->dbus_message_get_interface(msg);
  const char *member = mLib->dbus_message_get_member(msg);

  if (eq(iface, WATCHER_IFACE) && eq(member, "RegisterStatusNotifierItem"))
    registerItem(msg);
  else if (eq(iface, WATCHER_IFACE) && eq(member, "RegisterStatusNotifierHost"))
    registerHost(msg);
  else if (eq(iface, PROPS_IFACE) && eq(member, "Get"))
    getProperty(msg);
  else if (eq(iface, PROPS_IFACE) && eq(member, "GetAll"))
    getAllProperties(msg);
  else if (eq(iface, INTROSPECT_IFACE) && eq(member, "Introspect"))
    introspect(msg);
  else
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  return DBUS_HANDLER_RESULT_HANDLED;
}

void StatusNotifierWatcher::registerItem(DBusMessage *msg) {
  DBusLib *d;
  if ((d = mLib) == nullptr)
    return;
  const char *arg;
  if (!d->dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                                DBUS_TYPE_INVALID)) {
    sendReply(d->dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                        "Expected a single string argument"));
    return;
  }

  // Items may register either with a bus name (using the default object path)
  // or with just an object path on their own connection.
  std::string service, path;
  if (arg[0] == '/') {
    const char *sender = d->dbus_message_get_sender(msg);
    service = sender ? sender : "";
    path = arg;
  } else {
    service = arg;
    path = ITEM_DEFAULT_PATH;
  }

  if (service.empty() ||
      !d->dbus_bus_name_has_owner(mConn, service.c_str(), nullptr)) {
    sendReply(d->dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                        "Service is not on the bus"));
    return;
  }

  sendReply(d->dbus_message_new_method_return(msg));

  std::string item = service + path;
  if (std::find(mItems.begin(), mItems.end(), item) != mItems.end())
    return;
  mItems.push_back(item);
  emitSignal("StatusNotifierItemRegistered", item);
  itemsChanged();
}

void StatusNotifierWatcher::registerHost(DBusMessage *msg) {
  DBusLib *d;
  if ((d = mLib) == nullptr)
    return;
  const char *arg;
  if (!d->dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                                DBUS_TYPE_INVALID)) {
    sendReply(d->dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                        "Expected a single string argument"));
    return;
  }

  sendReply(d->dbus_message_new_method_return(msg));

  if (std::find(mHosts.begin(), mHosts.end(), arg) != mHosts.end())
    return;
  mHosts.emplace_back(arg);
  emitSignal("StatusNotifierHostRegistered", {});
}

void StatusNotifierWatcher::appendPropertyValue(DBusMessageIter *iter,
                                                const std::string &prop) {
  DBusLib *d;
  if ((d = mLib) == nullptr)
    return;
  DBusMessageIter variant, array;

  if (prop == "RegisteredStatusNotifierItems") {
    d->dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as",
                                        &variant);
    d->dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array);
    for (const auto &item : mItems) {
      const char *str = item.c_str();
      d->dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &str);
    }
    d->dbus_message_iter_close_container(&variant, &array);
  } else if (prop == "IsStatusNotifierHostRegistered") {
    // We are always a host ourselves.
    dbus_bool_t value = true;
    d->dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &variant);
    d->dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
  } else {
    dbus_int32_t value = 0;
    d->dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "i", &variant);
    d->dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &value);
  }
  d->dbus_message_iter_close_container(iter, &variant);
}

void StatusNotifierWatcher::getProperty(DBusMessage *msg) {
  DBusLib *d;
  if ((d = mLib) == nullptr)
    return;
  const char *iface, *prop;
  if (!d->dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface,
                                DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID)) {
    sendReply(d->dbus_message_new_error(
        msg, DBUS_ERROR_INVALID_ARGS, "Expected interface and property name"));
    return;
  }
  if (!eq(iface, WATCHER_IFACE) ||
      !(eq(prop, "RegisteredStatusNotifierItems") ||
        eq(prop, "IsStatusNotifierHostRegistered") ||
        eq(prop, "ProtocolVersion"))) {
    sendReply(d->dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY,
                                        "No such property"));
    return;
  }

  DBusMessage *reply = d->dbus_message_new_method_return(msg);
  DBusMessageIter iter;
  d->dbus_message_iter_init_append(reply, &iter);
  appendPropertyValue(&iter, prop);
  sendReply(reply);
}

void StatusNotifierWatcher::getAllProperties(DBusMessage *msg) {
  DBusLib *d;
  if ((d = mLib) == nullptr)
    return;
  const char *iface;
  if (!d->dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface,
                                DBUS_TYPE_INVALID)) {
    sendReply(d->dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                        "Expected interface name"));
    return;
  }

  DBusMessage *reply = d->dbus_message_new_method_return(msg);
  DBusMessageIter iter, dict, entry;
  d->dbus_message_iter_init_append(reply, &iter);
  d->dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
  if (eq(iface, WATCHER_IFACE)) {
    for (const char *prop :
         {"RegisteredStatusNotifierItems", "IsStatusNotifierHostRegistered",
          "ProtocolVersion"}) {
      d->dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr,
                                          &entry);
      d->dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &prop);
      appendPropertyValue(&entry, prop);
      d->dbus_message_iter_close_container(&dict, &entry);
    }
  }
  d->dbus_message_iter_close_container(&iter, &dict);
  sendReply(reply);
}

void StatusNotifierWatcher::introspect(DBusMessage *msg) {
  DBusMessage *reply = mLib->dbus_message_new_method_return(msg);
  const char *xml = INTROSPECT_XML;
  mLib->dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml,
                                 DBUS_TYPE_INVALID);
  sendReply(reply);
}

void StatusNotifierWatcher::emitSignal(const char *member,
                                       const std::string &arg) {
  if (!mConn)
    return;
  DBusLib *d;
  if ((d = mLib) == nullptr)
    return;
  DBusMessage *sig =
      d->dbus_message_new_signal(WATCHER_PATH, WATCHER_IFACE, member);
  if (!arg.empty()) {
    const char *str = arg.c_str();
    d->dbus_message_append_args(sig, DBUS_TYPE_STRING, &str, DBUS_TYPE_INVALID);
  }
  d->dbus_connection_send(mConn, sig, nullptr);
  d->dbus_message_unref(sig);
}

void StatusNotifierWatcher::sendReply(DBusMessage *reply) {
  if (!reply)
    return;
  mLib->dbus_connection_send(mConn, reply, nullptr);
  mLib->dbus_message_unref(reply);
}
#endif
