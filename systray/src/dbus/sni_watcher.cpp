#ifdef TCC_HAS_DBUS
#include "sni_watcher.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <unistd.h>

constexpr const char *WATCHER_NAME = "org.kde.StatusNotifierWatcher";
constexpr const char *WATCHER_IFACE = "org.kde.StatusNotifierWatcher";
constexpr const char *WATCHER_PATH = "/StatusNotifierWatcher";
constexpr const char *ITEM_IFACE = "org.kde.StatusNotifierItem";
constexpr const char *MENU_IFACE = "com.canonical.dbusmenu";
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

StatusNotifierWatcher::~StatusNotifierWatcher() {
  setItemsChangedCallback(nullptr);
  disconnect();
}

bool StatusNotifierWatcher::connect() {
  if (mConn)
    return true;

  dbus_error_init(&mDBusErr);

  mConn = dbus_bus_get_private(DBUS_BUS_SESSION, &mDBusErr);
  if (!mConn) {
    fprintf(stderr, "tcc_systray: could not connect to session bus: %s\n",
            dbus_error_is_set(&mDBusErr) ? mDBusErr.message : "unknown error");
    dbus_error_free(&mDBusErr);
    return false;
  }
  dbus_connection_set_exit_on_disconnect(mConn, false);
  mUniqueName = dbus_bus_get_unique_name(mConn);

  dbus_connection_add_filter(mConn, filterThunk, this, nullptr);
  dbus_bus_add_match(mConn,
                     "type='signal',sender='org.freedesktop.DBus',"
                     "interface='org.freedesktop.DBus',"
                     "member='NameOwnerChanged'",
                     nullptr);
  dbus_bus_add_match(mConn,
                     "type='signal',interface='org.kde.StatusNotifierWatcher'",
                     nullptr);
  dbus_bus_add_match(
      mConn, "type='signal',interface='org.kde.StatusNotifierItem'", nullptr);

  // Every host owns a name of this form so that the watcher (and items) can
  // tell when it goes away.
  mHostName = HOST_NAME_PREFIX + std::to_string(getpid());
  dbus_bus_request_name(mConn, mHostName.c_str(), DBUS_NAME_FLAG_DO_NOT_QUEUE,
                        nullptr);

  // No DO_NOT_QUEUE: if someone else is the watcher we wait in line and the
  // bus hands us the name (via NameAcquired) once they exit.
  int ret = dbus_bus_request_name(mConn, WATCHER_NAME, 0, &mDBusErr);
  if (dbus_error_is_set(&mDBusErr)) {
    fprintf(stderr, "tcc_systray: could not request %s: %s\n", WATCHER_NAME,
            mDBusErr.message);
    dbus_error_free(&mDBusErr);
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
  bool hadItems = !mItems.empty();
  clearItems();
  for (DBusPendingCall *pending : mItemCalls) {
    dbus_pending_call_cancel(pending);
    dbus_pending_call_unref(pending);
  }
  mItemCalls.clear();
  dbus_connection_close(mConn);
  dbus_connection_unref(mConn);
  mConn = nullptr;
  mMode = Mode::Disconnected;
  mHosts.clear();
  if (hadItems)
    itemsChanged();
}

int StatusNotifierWatcher::fd() const {
  int fd = -1;
  if (mConn)
    dbus_connection_get_unix_fd(mConn, &fd);
  return fd;
}

void StatusNotifierWatcher::poll() {
  if (!mConn)
    return;
  DBusLib *d;

  if (!dbus_connection_read_write(mConn, 0)) {
    fprintf(stderr, "tcc_systray: lost connection to session bus\n");
    disconnect();
    return;
  }
  while (dbus_connection_dispatch(mConn) == DBUS_DISPATCH_DATA_REMAINS) {
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
    clearItems();
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

  DBusMessage *msg = dbus_message_new_method_call(
      WATCHER_NAME, WATCHER_PATH, WATCHER_IFACE, "RegisterStatusNotifierHost");
  const char *host = mHostName.c_str();
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &host, DBUS_TYPE_INVALID);
  dbus_message_set_no_reply(msg, true);
  dbus_connection_send(mConn, msg, nullptr);
  dbus_message_unref(msg);

  msg = dbus_message_new_method_call(WATCHER_NAME, WATCHER_PATH, PROPS_IFACE,
                                     "Get");
  const char *iface = WATCHER_IFACE;
  const char *prop = "RegisteredStatusNotifierItems";
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING,
                           &prop, DBUS_TYPE_INVALID);

  DBusMessage *reply =
      dbus_connection_send_with_reply_and_block(mConn, msg, 1000, &mDBusErr);
  dbus_message_unref(msg);

  std::vector<std::string> ids;
  if (!reply) {
    fprintf(stderr, "tcc_systray: could not get items from %s: %s\n",
            WATCHER_NAME, mDBusErr.message);
    dbus_error_free(&mDBusErr);
  } else {
    DBusMessageIter iter, variant, array;
    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
      dbus_message_iter_recurse(&iter, &variant);
      if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&variant, &array);
        while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
          const char *item;
          dbus_message_iter_get_basic(&array, &item);
          ids.emplace_back(item);
          dbus_message_iter_next(&array);
        }
      }
    }
    dbus_message_unref(reply);
  }

  bool removed = false;
  for (size_t i = mItems.size(); i-- > 0;) {
    if (std::find(ids.begin(), ids.end(), mItems[i].id) == ids.end()) {
      removeItem(mItems[i].id);
      removed = true;
    }
  }
  for (const auto &id : ids)
    addItem(id);
  if (removed)
    itemsChanged();
}

DBusHandlerResult StatusNotifierWatcher::filterThunk(DBusConnection *,
                                                     DBusMessage *msg,
                                                     void *self) {
  return static_cast<StatusNotifierWatcher *>(self)->filter(msg);
}

DBusHandlerResult StatusNotifierWatcher::filter(DBusMessage *msg) {
  DBusLib *d;
  const char *arg = nullptr;

  if (dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
    const char *name, *oldOwner, *newOwner;
    if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_STRING, &oldOwner, DBUS_TYPE_STRING,
                              &newOwner, DBUS_TYPE_INVALID))
      handleNameOwnerChanged(name, oldOwner, newOwner);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameAcquired")) {
    if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                              DBUS_TYPE_INVALID) &&
        eq(arg, WATCHER_NAME))
      becomeWatcher();
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameLost")) {
    if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                              DBUS_TYPE_INVALID) &&
        eq(arg, WATCHER_NAME) && mMode == Mode::Watcher)
      becomeHost();
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  // While we are the watcher our own signals are echoed back to us; ignore
  // them.
  if (mMode == Mode::Host) {
    bool registered = dbus_message_is_signal(msg, WATCHER_IFACE,
                                             "StatusNotifierItemRegistered");
    bool unregistered = dbus_message_is_signal(
        msg, WATCHER_IFACE, "StatusNotifierItemUnregistered");
    if ((registered || unregistered) &&
        dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                              DBUS_TYPE_INVALID)) {
      if (registered) {
        addItem(arg);
      } else if (findItem(arg)) {
        removeItem(arg);
        itemsChanged();
      }
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }

  if (handleItemSignal(msg))
    return DBUS_HANDLER_RESULT_HANDLED;

  if (mMode == Mode::Watcher &&
      dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL &&
      eq(dbus_message_get_path(msg), WATCHER_PATH))
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
        clearItems();
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
  for (size_t i = mItems.size(); i-- > 0;) {
    std::string id = mItems[i].id;
    if (mItemConns[id].service == name) {
      emitSignal("StatusNotifierItemUnregistered", id);
      removeItem(id);
      changed = true;
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
  const char *iface = dbus_message_get_interface(msg);
  const char *member = dbus_message_get_member(msg);

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
  const char *arg;
  if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                             DBUS_TYPE_INVALID)) {
    sendReply(dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                     "Expected a single string argument"));
    return;
  }

  // Items may register either with a bus name (using the default object path)
  // or with just an object path on their own connection.
  std::string service, path;
  if (arg[0] == '/') {
    const char *sender = dbus_message_get_sender(msg);
    service = sender ? sender : "";
    path = arg;
  } else {
    service = arg;
    path = ITEM_DEFAULT_PATH;
  }

  if (service.empty() ||
      !dbus_bus_name_has_owner(mConn, service.c_str(), nullptr)) {
    sendReply(dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                     "Service is not on the bus"));
    return;
  }

  sendReply(dbus_message_new_method_return(msg));

  std::string id = service + path;
  if (findItem(id))
    return;
  addItem(id);
  emitSignal("StatusNotifierItemRegistered", id);
}

void StatusNotifierWatcher::registerHost(DBusMessage *msg) {
  DBusLib *d;
  const char *arg;
  if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg,
                             DBUS_TYPE_INVALID)) {
    sendReply(dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                     "Expected a single string argument"));
    return;
  }

  sendReply(dbus_message_new_method_return(msg));

  if (std::find(mHosts.begin(), mHosts.end(), arg) != mHosts.end())
    return;
  mHosts.emplace_back(arg);
  emitSignal("StatusNotifierHostRegistered", {});
}

void StatusNotifierWatcher::appendPropertyValue(DBusMessageIter *iter,
                                                const std::string &prop) {
  DBusLib *d;
  DBusMessageIter variant, array;

  if (prop == "RegisteredStatusNotifierItems") {
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array);
    for (const auto &item : mItems) {
      const char *str = item.id.c_str();
      dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &str);
    }
    dbus_message_iter_close_container(&variant, &array);
  } else if (prop == "IsStatusNotifierHostRegistered") {
    // We are always a host ourselves.
    dbus_bool_t value = true;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
  } else {
    dbus_int32_t value = 0;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "i", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &value);
  }
  dbus_message_iter_close_container(iter, &variant);
}

void StatusNotifierWatcher::getProperty(DBusMessage *msg) {
  DBusLib *d;
  const char *iface, *prop;
  if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID)) {
    sendReply(dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                     "Expected interface and property name"));
    return;
  }
  if (!eq(iface, WATCHER_IFACE) ||
      !(eq(prop, "RegisteredStatusNotifierItems") ||
        eq(prop, "IsStatusNotifierHostRegistered") ||
        eq(prop, "ProtocolVersion"))) {
    sendReply(dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY,
                                     "No such property"));
    return;
  }

  DBusMessage *reply = dbus_message_new_method_return(msg);
  DBusMessageIter iter;
  dbus_message_iter_init_append(reply, &iter);
  appendPropertyValue(&iter, prop);
  sendReply(reply);
}

void StatusNotifierWatcher::getAllProperties(DBusMessage *msg) {
  DBusLib *d;
  const char *iface;
  if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_INVALID)) {
    sendReply(dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                     "Expected interface name"));
    return;
  }

  DBusMessage *reply = dbus_message_new_method_return(msg);
  DBusMessageIter iter, dict, entry;
  dbus_message_iter_init_append(reply, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
  if (eq(iface, WATCHER_IFACE)) {
    for (const char *prop :
         {"RegisteredStatusNotifierItems", "IsStatusNotifierHostRegistered",
          "ProtocolVersion"}) {
      dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr,
                                       &entry);
      dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &prop);
      appendPropertyValue(&entry, prop);
      dbus_message_iter_close_container(&dict, &entry);
    }
  }
  dbus_message_iter_close_container(&iter, &dict);
  sendReply(reply);
}

void StatusNotifierWatcher::introspect(DBusMessage *msg) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  const char *xml = INTROSPECT_XML;
  dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
  sendReply(reply);
}

void StatusNotifierWatcher::emitSignal(const char *member,
                                       const std::string &arg) {
  if (!mConn)
    return;
  DBusLib *d;
  DBusMessage *sig =
      dbus_message_new_signal(WATCHER_PATH, WATCHER_IFACE, member);
  if (!arg.empty()) {
    const char *str = arg.c_str();
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &str, DBUS_TYPE_INVALID);
  }
  dbus_connection_send(mConn, sig, nullptr);
  dbus_message_unref(sig);
}

void StatusNotifierWatcher::sendReply(DBusMessage *reply) {
  if (!reply)
    return;
  dbus_connection_send(mConn, reply, nullptr);
  dbus_message_unref(reply);
}

SystrayItem *StatusNotifierWatcher::findItem(const std::string &id) {
  auto it =
      std::find_if(mItems.begin(), mItems.end(),
                   [&](const SystrayItem &item) { return item.id == id; });
  return it == mItems.end() ? nullptr : &*it;
}

void StatusNotifierWatcher::addItem(const std::string &id) {
  if (findItem(id))
    return;

  // Ids are "<service><path>". Some watchers only give the service, in which
  // case the item is at the default path.
  ItemConn conn;
  auto slash = id.find('/');
  if (slash == std::string::npos) {
    conn.service = id;
    conn.path = ITEM_DEFAULT_PATH;
  } else {
    conn.service = id.substr(0, slash);
    conn.path = id.substr(slash);
  }
  if (!conn.service.empty() && conn.service[0] == ':')
    conn.owner = conn.service;

  SystrayItem item;
  item.id = id;
  mItems.push_back(std::move(item));
  mItemConns[id] = std::move(conn);
  fetchItemProperties(id);
}

void StatusNotifierWatcher::removeItem(const std::string &id) {
  auto conn = mItemConns.find(id);
  if (conn != mItemConns.end()) {
    if (conn->second.pending) {
      dbus_pending_call_cancel(conn->second.pending);
      dbus_pending_call_unref(conn->second.pending);
    }
    mItemConns.erase(conn);
  }
  std::erase_if(mItems, [&](const SystrayItem &item) { return item.id == id; });
}

void StatusNotifierWatcher::clearItems() {
  while (!mItems.empty())
    removeItem(mItems.back().id);
}

namespace {
struct PendingProperties {
  StatusNotifierWatcher *self;
  std::string id;
};
} // namespace

void StatusNotifierWatcher::fetchItemProperties(const std::string &id) {
  DBusLib *d;
  auto &conn = mItemConns.at(id);

  // A newer fetch supersedes an older one.
  if (conn.pending) {
    dbus_pending_call_cancel(conn.pending);
    dbus_pending_call_unref(conn.pending);
    conn.pending = nullptr;
  }

  DBusMessage *msg = dbus_message_new_method_call(
      conn.service.c_str(), conn.path.c_str(), PROPS_IFACE, "GetAll");
  const char *iface = ITEM_IFACE;
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);

  // Asynchronous, so an unresponsive item can't hang the tray.
  DBusPendingCall *pending = nullptr;
  if (dbus_connection_send_with_reply(mConn, msg, &pending, 5000) && pending) {
    conn.pending = pending;
    dbus_pending_call_set_notify(
        pending, propertiesReplyThunk, new PendingProperties{this, id},
        [](void *data) { delete static_cast<PendingProperties *>(data); });
  }
  dbus_message_unref(msg);
}

void StatusNotifierWatcher::propertiesReplyThunk(DBusPendingCall *pending,
                                                 void *data) {
  // Copied out, since releasing the pending call frees data.
  auto [self, id] = *static_cast<PendingProperties *>(data);

  DBusMessage *reply = dbus_pending_call_steal_reply(pending);
  auto conn = self->mItemConns.find(id);
  if (conn != self->mItemConns.end() && conn->second.pending == pending)
    conn->second.pending = nullptr;
  dbus_pending_call_unref(pending);

  if (reply) {
    self->propertiesReply(id, reply);
    dbus_message_unref(reply);
  }
}

void StatusNotifierWatcher::propertiesReply(const std::string &id,
                                            DBusMessage *reply) {
  DBusLib *d;
  SystrayItem *item = findItem(id);
  if (!item)
    return;

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    // Still show the item, with whatever we knew before.
    fprintf(stderr, "tcc_systray: could not get properties of %s\n",
            id.c_str());
    itemsChanged();
    return;
  }

  if (const char *sender = dbus_message_get_sender(reply))
    mItemConns[id].owner = sender;

  SystrayItem updated;
  updated.id = id;

  DBusMessageIter iter, dict, entry, value;
  if (dbus_message_iter_init(reply, &iter) &&
      dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
    dbus_message_iter_recurse(&iter, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
      const char *prop;
      dbus_message_iter_recurse(&dict, &entry);
      dbus_message_iter_get_basic(&entry, &prop);
      dbus_message_iter_next(&entry);
      dbus_message_iter_recurse(&entry, &value);
      if (eq(prop, "Menu") &&
          dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_OBJECT_PATH) {
        const char *path;
        dbus_message_iter_get_basic(&value, &path);
        // Some items use these to say they have no menu.
        mItemConns[id].menuPath =
            eq(path, "/") || eq(path, "/NO_DBUSMENU") ? "" : path;
      }
      readItemProperty(updated, prop, &value);
      dbus_message_iter_next(&dict);
    }
  }

  // The theme path applies to all of the item's named icons.
  updated.attentionIcon.themePath = updated.icon.themePath;
  updated.overlayIcon.themePath = updated.icon.themePath;

  if (updated == *item)
    return;
  *item = std::move(updated);
  itemsChanged();
}

void StatusNotifierWatcher::readItemProperty(SystrayItem &item,
                                             const char *prop,
                                             DBusMessageIter *value) {
  int type = dbus_message_iter_get_arg_type(value);

  if (type == DBUS_TYPE_STRING) {
    const char *str;
    dbus_message_iter_get_basic(value, &str);
    if (eq(prop, "Title"))
      item.title = str;
    else if (eq(prop, "Status"))
      item.status = eq(str, "Passive") ? SystrayItem::Status::Passive
                    : eq(str, "NeedsAttention")
                        ? SystrayItem::Status::NeedsAttention
                        : SystrayItem::Status::Active;
    else if (eq(prop, "IconName"))
      item.icon.name = str;
    else if (eq(prop, "IconThemePath"))
      item.icon.themePath = str;
    else if (eq(prop, "AttentionIconName"))
      item.attentionIcon.name = str;
    else if (eq(prop, "OverlayIconName"))
      item.overlayIcon.name = str;
  } else if (type == DBUS_TYPE_BOOLEAN) {
    dbus_bool_t b;
    dbus_message_iter_get_basic(value, &b);
    if (eq(prop, "ItemIsMenu"))
      item.itemIsMenu = b;
  } else if (type == DBUS_TYPE_ARRAY) {
    if (eq(prop, "IconPixmap"))
      readPixmaps(item.icon, value);
    else if (eq(prop, "AttentionIconPixmap"))
      readPixmaps(item.attentionIcon, value);
    else if (eq(prop, "OverlayIconPixmap"))
      readPixmaps(item.overlayIcon, value);
  }
}

// Reads an a(iiay) list of ARGB32 images, keeping the one that best fits
// mIconSize: the smallest that is at least that big, or else the largest.
void StatusNotifierWatcher::readPixmaps(SystrayIcon &icon,
                                        DBusMessageIter *value) {
  DBusMessageIter array, strct, bytes;
  const unsigned char *best = nullptr;
  int bestWidth = 0, bestHeight = 0;

  dbus_message_iter_recurse(value, &array);
  while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
    dbus_int32_t width, height;
    const unsigned char *data = nullptr;
    int len = 0;

    dbus_message_iter_recurse(&array, &strct);
    dbus_message_iter_next(&array);
    if (dbus_message_iter_get_arg_type(&strct) != DBUS_TYPE_INT32)
      continue;
    dbus_message_iter_get_basic(&strct, &width);
    dbus_message_iter_next(&strct);
    if (dbus_message_iter_get_arg_type(&strct) != DBUS_TYPE_INT32)
      continue;
    dbus_message_iter_get_basic(&strct, &height);
    dbus_message_iter_next(&strct);
    if (dbus_message_iter_get_arg_type(&strct) != DBUS_TYPE_ARRAY)
      continue;
    dbus_message_iter_recurse(&strct, &bytes);
    dbus_message_iter_get_fixed_array(&bytes, &data, &len);

    if (width <= 0 || height <= 0 || !data ||
        (long)len != (long)width * height * 4)
      continue;

    bool bigEnough = width >= mIconSize && height >= mIconSize;
    bool bestBigEnough = bestWidth >= mIconSize && bestHeight >= mIconSize;
    if (!best || (bigEnough && (!bestBigEnough || width < bestWidth)) ||
        (!bigEnough && !bestBigEnough && width > bestWidth)) {
      best = data;
      bestWidth = width;
      bestHeight = height;
    }
  }

  if (!best)
    return;

  // ARGB in network byte order to RGBA.
  icon.width = bestWidth;
  icon.height = bestHeight;
  icon.pixels.resize((size_t)bestWidth * bestHeight * 4);
  for (size_t i = 0; i < icon.pixels.size(); i += 4) {
    icon.pixels[i + 0] = best[i + 1];
    icon.pixels[i + 1] = best[i + 2];
    icon.pixels[i + 2] = best[i + 3];
    icon.pixels[i + 3] = best[i + 0];
  }
}

// Refetches an item's properties when it says they changed.
bool StatusNotifierWatcher::handleItemSignal(DBusMessage *msg) {
  if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL ||
      !eq(dbus_message_get_interface(msg), ITEM_IFACE))
    return false;

  const char *member = dbus_message_get_member(msg);
  if (!(eq(member, "NewIcon") || eq(member, "NewAttentionIcon") ||
        eq(member, "NewOverlayIcon") || eq(member, "NewStatus") ||
        eq(member, "NewTitle")))
    return false;

  const char *sender = dbus_message_get_sender(msg);
  const char *path = dbus_message_get_path(msg);
  for (auto &[id, conn] : mItemConns) {
    if (eq(conn.owner.c_str(), sender) && eq(conn.path.c_str(), path)) {
      fetchItemProperties(id);
      return true;
    }
  }
  return false;
}

struct StatusNotifierWatcher::ItemCall {
  StatusNotifierWatcher *self;
  std::function<void(DBusMessage *reply)> onReply;
};

void StatusNotifierWatcher::callAsync(
    DBusMessage *msg, std::function<void(DBusMessage *reply)> onReply) {
  DBusPendingCall *pending = nullptr;
  if (dbus_connection_send_with_reply(mConn, msg, &pending, 5000) && pending) {
    mItemCalls.push_back(pending);
    dbus_pending_call_set_notify(
        pending, itemCallReplyThunk, new ItemCall{this, std::move(onReply)},
        [](void *data) { delete static_cast<ItemCall *>(data); });
  }
}

void StatusNotifierWatcher::itemCallReplyThunk(DBusPendingCall *pending,
                                               void *data) {
  // Copied out, since releasing the pending call frees data.
  ItemCall call = *static_cast<ItemCall *>(data);
  StatusNotifierWatcher *self = call.self;

  DBusMessage *reply = dbus_pending_call_steal_reply(pending);
  std::erase(self->mItemCalls, pending);
  dbus_pending_call_unref(pending);

  call.onReply(reply);
  if (reply)
    dbus_message_unref(reply);
}

void StatusNotifierWatcher::callItemMethod(const std::string &id,
                                           const char *method, int x, int y,
                                           std::function<void()> onError) {
  DBusLib *d;
  if (!mConn)
    return;
  auto conn = mItemConns.find(id);
  if (conn == mItemConns.end())
    return;

  DBusMessage *msg = dbus_message_new_method_call(conn->second.service.c_str(),
                                                  conn->second.path.c_str(),
                                                  ITEM_IFACE, method);
  dbus_int32_t dx = x, dy = y;
  dbus_message_append_args(msg, DBUS_TYPE_INT32, &dx, DBUS_TYPE_INT32, &dy,
                           DBUS_TYPE_INVALID);

  if (!onError) {
    dbus_message_set_no_reply(msg, true);
    dbus_connection_send(mConn, msg, nullptr);
  } else {
    callAsync(msg, [d, onError](DBusMessage *reply) {
      if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
        onError();
    });
  }
  dbus_message_unref(msg);
}

void StatusNotifierWatcher::activate(const std::string &id, int x, int y) {
  SystrayItem *item = findItem(id);
  if (!item)
    return;
  if (item->itemIsMenu) {
    contextMenu(id, x, y);
    return;
  }
  // Items that only have a menu often don't implement Activate.
  callItemMethod(id, "Activate", x, y, [this, id, x, y] {
    if (findItem(id))
      contextMenu(id, x, y);
  });
}

void StatusNotifierWatcher::secondaryActivate(const std::string &id, int x,
                                              int y) {
  callItemMethod(id, "SecondaryActivate", x, y);
}

void StatusNotifierWatcher::contextMenu(const std::string &id, int x, int y) {
  auto conn = mItemConns.find(id);
  if (conn == mItemConns.end())
    return;

  // Like Plasma, show the item's dbusmenu ourselves when it has one, and only
  // otherwise ask the item to show its own menu.
  if (!conn->second.menuPath.empty()) {
    fetchMenu(id);
    return;
  }
  callItemMethod(id, "ContextMenu", x, y, [id] {
    fprintf(stderr, "tcc_systray: %s has no menu\n", id.c_str());
  });
}

void StatusNotifierWatcher::fetchMenu(const std::string &id) {
  const ItemConn &conn = mItemConns.at(id);

  // Lets the item update the menu before we fetch it.
  DBusMessage *msg = dbus_message_new_method_call(
      conn.service.c_str(), conn.menuPath.c_str(), MENU_IFACE, "AboutToShow");
  dbus_int32_t root = 0;
  dbus_message_append_args(msg, DBUS_TYPE_INT32, &root, DBUS_TYPE_INVALID);
  dbus_message_set_no_reply(msg, true);
  dbus_connection_send(mConn, msg, nullptr);
  dbus_message_unref(msg);

  // GetLayout(parentId, recursionDepth, propertyNames): the whole tree, with
  // all properties.
  msg = dbus_message_new_method_call(
      conn.service.c_str(), conn.menuPath.c_str(), MENU_IFACE, "GetLayout");
  dbus_int32_t depth = -1;
  DBusMessageIter iter, props;
  dbus_message_iter_init_append(msg, &iter);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &root);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &depth);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &props);
  dbus_message_iter_close_container(&iter, &props);

  callAsync(msg, [this, id](DBusMessage *reply) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
      fprintf(stderr, "tcc_systray: could not get the menu of %s\n",
              id.c_str());
      return;
    }
    if (!findItem(id))
      return;

    // (u revision, (ia{sv}av) layout)
    DBusMessageIter iter;
    SystrayMenuEntry root;
    bool visible;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32)
      return;
    dbus_message_iter_next(&iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRUCT)
      return;
    readMenuLayout(&iter, root, &visible);
    if (root.children.empty())
      return;

    sendMenuEvent(id, 0, "opened");
    showMenu(id, root.children);
  });
  dbus_message_unref(msg);
}

// Reads one (ia{sv}av) node of a dbusmenu layout, and its children.
void StatusNotifierWatcher::readMenuLayout(DBusMessageIter *layout,
                                           SystrayMenuEntry &entry,
                                           bool *visible) {
  DBusMessageIter strct, props, prop, value, children, child;
  dbus_int32_t id = 0;
  *visible = true;

  dbus_message_iter_recurse(layout, &strct);
  if (dbus_message_iter_get_arg_type(&strct) != DBUS_TYPE_INT32)
    return;
  dbus_message_iter_get_basic(&strct, &id);
  entry.id = id;
  dbus_message_iter_next(&strct);

  if (dbus_message_iter_get_arg_type(&strct) != DBUS_TYPE_ARRAY)
    return;
  dbus_message_iter_recurse(&strct, &props);
  while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
    const char *name;
    dbus_message_iter_recurse(&props, &prop);
    dbus_message_iter_get_basic(&prop, &name);
    dbus_message_iter_next(&prop);
    dbus_message_iter_recurse(&prop, &value);
    int type = dbus_message_iter_get_arg_type(&value);

    if (type == DBUS_TYPE_STRING) {
      const char *str;
      dbus_message_iter_get_basic(&value, &str);
      if (eq(name, "type") && eq(str, "separator")) {
        entry.type = SystrayMenuEntry::Type::Separator;
      } else if (eq(name, "label")) {
        // Drop mnemonic underscores, "__" is a literal underscore.
        for (const char *c = str; *c; c++) {
          if (*c == '_' && c[1] == '_')
            entry.label += *++c;
          else if (*c != '_')
            entry.label += *c;
        }
      } else if (eq(name, "toggle-type")) {
        entry.toggle = eq(str, "checkmark")
                           ? SystrayMenuEntry::Toggle::Checkmark
                       : eq(str, "radio") ? SystrayMenuEntry::Toggle::Radio
                                          : SystrayMenuEntry::Toggle::None;
      }
    } else if (type == DBUS_TYPE_BOOLEAN) {
      dbus_bool_t b;
      dbus_message_iter_get_basic(&value, &b);
      if (eq(name, "enabled"))
        entry.enabled = b;
      else if (eq(name, "visible"))
        *visible = b;
    } else if (type == DBUS_TYPE_INT32) {
      dbus_int32_t i;
      dbus_message_iter_get_basic(&value, &i);
      if (eq(name, "toggle-state"))
        entry.toggled = i == 1;
    }
    dbus_message_iter_next(&props);
  }
  dbus_message_iter_next(&strct);

  if (dbus_message_iter_get_arg_type(&strct) != DBUS_TYPE_ARRAY)
    return;
  dbus_message_iter_recurse(&strct, &children);
  while (dbus_message_iter_get_arg_type(&children) == DBUS_TYPE_VARIANT) {
    dbus_message_iter_recurse(&children, &child);
    if (dbus_message_iter_get_arg_type(&child) == DBUS_TYPE_STRUCT) {
      SystrayMenuEntry sub;
      bool subVisible;
      readMenuLayout(&child, sub, &subVisible);
      if (subVisible)
        entry.children.push_back(std::move(sub));
    }
    dbus_message_iter_next(&children);
  }
}

// Event(id, eventId, data, timestamp)
void StatusNotifierWatcher::sendMenuEvent(const std::string &id, int entryId,
                                          const char *event) {
  DBusLib *d;
  auto conn = mItemConns.find(id);
  if (conn == mItemConns.end() || conn->second.menuPath.empty())
    return;

  DBusMessage *msg = dbus_message_new_method_call(conn->second.service.c_str(),
                                                  conn->second.menuPath.c_str(),
                                                  MENU_IFACE, "Event");
  DBusMessageIter iter, variant;
  dbus_int32_t did = entryId, data = 0;
  dbus_uint32_t timestamp = 0;
  dbus_message_iter_init_append(msg, &iter);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &did);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &event);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "i", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &data);
  dbus_message_iter_close_container(&iter, &variant);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &timestamp);
  dbus_message_set_no_reply(msg, true);
  dbus_connection_send(mConn, msg, nullptr);
  dbus_message_unref(msg);
}

void StatusNotifierWatcher::menuEntryActivated(const std::string &id,
                                               int entryId) {
  sendMenuEvent(id, entryId, "clicked");
}

void StatusNotifierWatcher::menuClosed(const std::string &id) {
  sendMenuEvent(id, 0, "closed");
}

void StatusNotifierWatcher::scroll(const std::string &id, int delta,
                                   bool horizontal) {
  DBusLib *d;
  auto conn = mItemConns.find(id);
  if (conn == mItemConns.end())
    return;

  DBusMessage *msg = dbus_message_new_method_call(conn->second.service.c_str(),
                                                  conn->second.path.c_str(),
                                                  ITEM_IFACE, "Scroll");
  dbus_int32_t ddelta = delta;
  const char *orientation = horizontal ? "horizontal" : "vertical";
  dbus_message_append_args(msg, DBUS_TYPE_INT32, &ddelta, DBUS_TYPE_STRING,
                           &orientation, DBUS_TYPE_INVALID);
  dbus_message_set_no_reply(msg, true);
  dbus_connection_send(mConn, msg, nullptr);
  dbus_message_unref(msg);
}
#endif
