#include "bridge.hpp"
#include "portal_dbus.h"

#include <cstdio>
#include <cstring>

static const char *OBJECT_PATH = "/org/freedesktop/portal/desktop";
static const char *DBUS_PREFIX = "org.freedesktop.impl.portal.";
static const char *VARLINK_PREFIX = "net.ioi-xd.tcc.portal.";
static const char *DBUS_ERROR_PREFIX = "org.freedesktop.portal.Error.";
static const char *PROPS_IFACE = "org.freedesktop.DBus.Properties";
static const char *INTROSPECT_IFACE = "org.freedesktop.DBus.Introspectable";

static const char *STANDARD_INTERFACES_XML =
    R"TCC_XML(  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect">
      <arg name="xml_data" type="s" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Get">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="property_name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="GetAll">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="properties" type="a{sv}" direction="out"/>
    </method>
    <method name="Set">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="property_name" type="s" direction="in"/>
      <arg name="value" type="v" direction="in"/>
    </method>
  </interface>
)TCC_XML";

struct TCCPortalBridge::Call {
  VarlinkConnection *conn = nullptr;
  // The D-Bus call this is for, if any.
  DBusMessage *msg = nullptr;
  TCCFds fds;
  bool done = false;
  std::function<void(const char *error, VarlinkObject *parameters)> on_reply;
  // The connection closed before the last reply.
  std::function<void()> on_lost;

  ~Call() {
    if (conn)
      varlink_connection_free(conn);
    if (msg)
      dbus_message_unref(msg);
  }
};

static bool eq(const char *a, const char *b) {
  return a && b && strcmp(a, b) == 0;
}

// Interfaces that are on an object of their own (the handle) rather than on
// OBJECT_PATH.
static bool per_object(const TCCDBusInterface &iface) {
  return iface.name == "org.freedesktop.impl.portal.Request" ||
         iface.name == "org.freedesktop.impl.portal.Session";
}

static std::string short_name(const TCCDBusInterface &iface) {
  return iface.name.substr(strlen(DBUS_PREFIX));
}

static DBusMessage *error_reply(DBusMessage *msg, const char *error,
                                VarlinkObject *parameters) {
  const char *message = nullptr;
  if (!parameters ||
      varlink_object_get_string(parameters, "message", &message) != 0)
    message = error;

  std::string name;
  const char *last = strrchr(error, '.');
  if (strncmp(error, VARLINK_PREFIX, strlen(VARLINK_PREFIX)) == 0 && last)
    name = std::string(DBUS_ERROR_PREFIX) + (last + 1);
  else if (eq(error, "org.varlink.service.MethodNotFound") ||
           eq(error, "org.varlink.service.InterfaceNotFound"))
    name = DBUS_ERROR_UNKNOWN_METHOD;
  else if (eq(error, "org.varlink.service.InvalidParameter"))
    name = DBUS_ERROR_INVALID_ARGS;
  else
    name = DBUS_ERROR_FAILED;
  return dbus_message_new_error(msg, name.c_str(), message);
}

static DBusMessage *bad_reply(DBusMessage *msg, const std::string &what) {
  std::string message =
      "tcc_portal: bad reply from the varlink service: " + what;
  fprintf(stderr, "%s\n", message.c_str());
  return dbus_message_new_error(msg, DBUS_ERROR_FAILED, message.c_str());
}

// Appends the dict entry for property `prop` from GetProperties' reply.
static bool append_property(VarlinkObject *properties,
                            const TCCDBusProperty &prop,
                            DBusMessageIter *iter) {
  VarlinkObject *variant;
  const char *signature;
  if (varlink_object_get_object(properties, prop.name.c_str(), &variant) != 0 ||
      varlink_object_get_string(variant, "signature", &signature) != 0 ||
      prop.type != signature)
    return false;
  return tcc_convert::to_dbus({properties, prop.name.c_str()}, "v", iter);
}

TCCPortalBridge::TCCPortalBridge(DBusConnection *bus, std::string address)
    : mBus(bus), mAddress(std::move(address)) {}

TCCPortalBridge::~TCCPortalBridge() = default;

bool TCCPortalBridge::start() {
  for (const char *xml : PORTAL_DBUS_XML) {
    if (!tcc_introspection::parse(xml, mInterfaces))
      return false;
  }

  std::string objects;
  for (const TCCDBusInterface &iface : mInterfaces) {
    (per_object(iface) ? objects : mIntrospectXML) += iface.to_xml();
  }
  std::string header = DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE "<node>\n";
  mObjectIntrospectXML =
      header + STANDARD_INTERFACES_XML + objects + "</node>\n";
  mIntrospectXML =
      header + STANDARD_INTERFACES_XML + mIntrospectXML + "</node>\n";

  if (!dbus_connection_add_filter(mBus, filter, this, nullptr))
    return false;

  for (const TCCDBusInterface &iface : mInterfaces) {
    if (!iface.signals.empty())
      subscribe(iface);
  }
  return true;
}

void TCCPortalBridge::send(DBusMessage *msg) {
  if (!msg)
    return;
  dbus_connection_send(mBus, msg, nullptr);
  dbus_message_unref(msg);
}

TCCPortalBridge::Call *TCCPortalBridge::call(const TCCDBusInterface &iface,
                                             const char *method,
                                             VarlinkObject *parameters,
                                             uint64_t flags) {
  auto call = std::make_unique<Call>();
  long error = varlink_connection_new(&call->conn, mAddress.c_str());
  if (error != 0) {
    fprintf(stderr, "tcc_portal: could not connect to %s: %s\n",
            mAddress.c_str(), varlink_error_string(-error));
    return nullptr;
  }

  Call *c = call.get();
  std::string name = VARLINK_PREFIX + short_name(iface) + "." + method;
  error = varlink_connection_call(
      c->conn, name.c_str(), parameters, flags,
      +[](VarlinkConnection *connection, const char *err, VarlinkObject *params,
          uint64_t reply_flags, void *userdata) -> long {
        Call *c = (Call *)userdata;
        if (!(reply_flags & VARLINK_REPLY_CONTINUES) || err)
          c->done = true;
        if (c->on_reply)
          c->on_reply(err, params);
        return 0;
      },
      c);
  if (error != 0) {
    fprintf(stderr, "tcc_portal: could not call %s: %s\n", name.c_str(),
            varlink_error_string(-error));
    return nullptr;
  }
  mCalls.push_back(std::move(call));
  return c;
}

void TCCPortalBridge::forward(DBusMessage *msg, const TCCDBusInterface &iface,
                              const TCCDBusMember &method) {
  VarlinkObject *parameters = nullptr;
  if (varlink_object_new(&parameters) != 0)
    return;
  if (per_object(iface))
    varlink_object_set_string(parameters, "path", dbus_message_get_path(msg));

  // Holds the fds until the call is done.
  auto fds = std::make_shared<TCCFds>();
  DBusMessageIter iter;
  bool more = dbus_message_iter_init(msg, &iter);
  std::string problem;
  for (const TCCDBusArg &arg : method.in) {
    char *signature = more ? dbus_message_iter_get_signature(&iter) : nullptr;
    bool matches = signature && arg.type == signature;
    dbus_free(signature);
    if (!matches) {
      problem = "Expected " + arg.type + " for " + arg.name;
      break;
    }
    if (!tcc_convert::from_dbus(&iter, {parameters, arg.name.c_str()}, *fds)) {
      problem = "Could not convert " + arg.name;
      break;
    }
    more = dbus_message_iter_next(&iter);
  }
  if (problem.empty() && more)
    problem = "Too many arguments";
  if (!problem.empty()) {
    send(dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, problem.c_str()));
    varlink_object_unref(parameters);
    return;
  }

  Call *c = call(iface, method.name.c_str(), parameters, 0);
  varlink_object_unref(parameters);
  if (!c) {
    send(dbus_message_new_error(msg, DBUS_ERROR_FAILED,
                                "tcc_portal: the varlink service is down"));
    return;
  }
  std::swap(c->fds.fds, fds->fds);
  c->msg = dbus_message_ref(msg);
  c->on_reply = [this, c, &method](const char *error, VarlinkObject *reply) {
    if (error) {
      send(error_reply(c->msg, error, reply));
      return;
    }
    DBusMessage *ret = dbus_message_new_method_return(c->msg);
    DBusMessageIter iter;
    dbus_message_iter_init_append(ret, &iter);
    for (const TCCDBusArg &arg : method.out) {
      if (!reply ||
          !tcc_convert::to_dbus({reply, arg.name.c_str()}, arg.type, &iter)) {
        dbus_message_unref(ret);
        ret = bad_reply(c->msg, method.name + "." + arg.name);
        break;
      }
    }
    send(ret);
  };
  c->on_lost = [this, c] {
    send(dbus_message_new_error(c->msg, DBUS_ERROR_FAILED,
                                "tcc_portal: the varlink service went away"));
  };
}

void TCCPortalBridge::properties(DBusMessage *msg, const char *member) {
  const char *iface_name = nullptr;
  const char *prop_name = nullptr;
  DBusMessageIter iter;
  bool get = eq(member, "Get");
  bool get_all = eq(member, "GetAll");
  bool set = eq(member, "Set");
  bool ok = dbus_message_iter_init(msg, &iter) &&
            dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING;
  if (ok) {
    dbus_message_iter_get_basic(&iter, &iface_name);
    if (get || set) {
      ok = dbus_message_iter_next(&iter) &&
           dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING;
      if (ok)
        dbus_message_iter_get_basic(&iter, &prop_name);
    }
    if (ok && set)
      ok = dbus_message_iter_next(&iter) &&
           dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT;
  }
  if (!(get || get_all || set)) {
    send(dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD,
                                "No such method"));
    return;
  }
  if (!ok) {
    send(dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                "Invalid arguments"));
    return;
  }

  const TCCDBusInterface *iface = nullptr;
  for (const TCCDBusInterface &i : mInterfaces) {
    if (i.name == iface_name)
      iface = &i;
  }
  if (!iface) {
    send(dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE,
                                "No such interface"));
    return;
  }
  const TCCDBusProperty *prop = nullptr;
  if (!get_all) {
    prop = iface->property(prop_name);
    if (!prop) {
      send(dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY,
                                  "No such property"));
      return;
    }
    if (set && !prop->writable) {
      send(dbus_message_new_error(msg, DBUS_ERROR_PROPERTY_READ_ONLY,
                                  "Property is read-only"));
      return;
    }
  }
  if (get_all && iface->properties.empty()) {
    DBusMessage *ret = dbus_message_new_method_return(msg);
    DBusMessageIter ret_iter, dict;
    dbus_message_iter_init_append(ret, &ret_iter);
    dbus_message_iter_open_container(&ret_iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dbus_message_iter_close_container(&ret_iter, &dict);
    send(ret);
    return;
  }

  VarlinkObject *parameters = nullptr;
  if (varlink_object_new(&parameters) != 0)
    return;
  if (per_object(*iface))
    varlink_object_set_string(parameters, "path", dbus_message_get_path(msg));
  TCCFds fds;
  if (set) {
    DBusMessageIter variant;
    dbus_message_iter_recurse(&iter, &variant);
    char *signature = dbus_message_iter_get_signature(&variant);
    ok = signature && prop->type == signature;
    dbus_free(signature);
    if (!ok) {
      varlink_object_unref(parameters);
      send(dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                  ("Expected " + prop->type).c_str()));
      return;
    }
    varlink_object_set_string(parameters, "name", prop_name);
    tcc_convert::from_dbus(&iter, {parameters, "value"}, fds);
  }

  Call *c = call(*iface, set ? "SetProperty" : "GetProperties", parameters, 0);
  varlink_object_unref(parameters);
  if (!c) {
    send(dbus_message_new_error(msg, DBUS_ERROR_FAILED,
                                "tcc_portal: the varlink service is down"));
    return;
  }
  std::swap(c->fds.fds, fds.fds);
  c->msg = dbus_message_ref(msg);
  c->on_reply = [this, c, iface, prop, set](const char *error,
                                            VarlinkObject *reply) {
    if (error) {
      send(error_reply(c->msg, error, reply));
      return;
    }
    DBusMessage *ret = dbus_message_new_method_return(c->msg);
    if (set) {
      send(ret);
      return;
    }

    VarlinkObject *props = nullptr;
    if (!reply || varlink_object_get_object(reply, "properties", &props) != 0) {
      dbus_message_unref(ret);
      send(bad_reply(c->msg, iface->name + " GetProperties"));
      return;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(ret, &iter);
    if (prop) {
      if (!append_property(props, *prop, &iter)) {
        dbus_message_unref(ret);
        ret = bad_reply(c->msg, iface->name + "." + prop->name);
      }
      send(ret);
      return;
    }

    DBusMessageIter dict;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
    for (const TCCDBusProperty &p : iface->properties) {
      DBusMessageIter entry;
      const char *name = p.name.c_str();
      dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr,
                                       &entry);
      dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
      if (!append_property(props, p, &entry)) {
        dbus_message_unref(ret);
        send(bad_reply(c->msg, iface->name + "." + p.name));
        return;
      }
      dbus_message_iter_close_container(&dict, &entry);
    }
    dbus_message_iter_close_container(&iter, &dict);
    send(ret);
  };
  c->on_lost = [this, c] {
    send(dbus_message_new_error(c->msg, DBUS_ERROR_FAILED,
                                "tcc_portal: the varlink service went away"));
  };
}

void TCCPortalBridge::subscribe(const TCCDBusInterface &iface) {
  VarlinkObject *parameters = nullptr;
  if (varlink_object_new(&parameters) != 0)
    return;
  Call *c = call(iface, "Subscribe", parameters, VARLINK_CALL_MORE);
  varlink_object_unref(parameters);
  if (!c)
    return;

  c->on_reply = [this, &iface](const char *error, VarlinkObject *reply) {
    if (error) {
      fprintf(stderr, "tcc_portal: no signals for %s: %s\n", iface.name.c_str(),
              error);
      return;
    }
    for (const TCCDBusMember &signal : iface.signals) {
      VarlinkObject *args;
      if (!reply ||
          varlink_object_get_object(reply, signal.name.c_str(), &args) != 0)
        continue;
      const char *path = OBJECT_PATH;
      if (per_object(iface))
        varlink_object_get_string(args, "path", &path);

      DBusMessage *msg = dbus_message_new_signal(path, iface.name.c_str(),
                                                 signal.name.c_str());
      if (!msg) {
        fprintf(stderr, "tcc_portal: bad path for %s: %s\n",
                signal.name.c_str(), path);
        continue;
      }
      DBusMessageIter iter;
      dbus_message_iter_init_append(msg, &iter);
      bool ok = true;
      for (const TCCDBusArg &arg : signal.out) {
        if (ok)
          ok = tcc_convert::to_dbus({args, arg.name.c_str()}, arg.type, &iter);
      }
      if (ok) {
        send(msg);
      } else {
        fprintf(stderr, "tcc_portal: bad %s.%s signal from the service\n",
                iface.name.c_str(), signal.name.c_str());
        dbus_message_unref(msg);
      }
    }
  };
  c->on_lost = [&iface] {
    fprintf(stderr, "tcc_portal: lost the %s signal subscription\n",
            iface.name.c_str());
  };
}

DBusHandlerResult TCCPortalBridge::filter(DBusConnection *bus, DBusMessage *msg,
                                          void *userdata) {
  TCCPortalBridge *bridge = (TCCPortalBridge *)userdata;
  const char *path = dbus_message_get_path(msg);
  // Request and Session objects are below OBJECT_PATH.
  size_t prefix = strlen(OBJECT_PATH);
  if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL || !path ||
      strncmp(path, OBJECT_PATH, prefix) != 0 ||
      (path[prefix] != '\0' && path[prefix] != '/'))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  bool main_object = path[prefix] == '\0';

  const char *iface_name = dbus_message_get_interface(msg);
  const char *member = dbus_message_get_member(msg);

  if (eq(iface_name, INTROSPECT_IFACE) && eq(member, "Introspect")) {
    const char *xml = main_object ? bridge->mIntrospectXML.c_str()
                                  : bridge->mObjectIntrospectXML.c_str();
    DBusMessage *ret = dbus_message_new_method_return(msg);
    dbus_message_append_args(ret, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    bridge->send(ret);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (eq(iface_name, PROPS_IFACE)) {
    bridge->properties(msg, member);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  for (const TCCDBusInterface &iface : bridge->mInterfaces) {
    if (!eq(iface_name, iface.name.c_str()))
      continue;
    if (const TCCDBusMember *method = iface.method(member)) {
      bridge->forward(msg, iface, *method);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }
  bridge->send(
      dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, "No such method"));
  return DBUS_HANDLER_RESULT_HANDLED;
}

void TCCPortalBridge::add_pollfds(std::vector<pollfd> &fds) {
  for (const auto &c : mCalls) {
    pollfd pfd = {};
    pfd.fd = varlink_connection_get_fd(c->conn);
    pfd.events = varlink_connection_get_events(c->conn);
    fds.push_back(pfd);
  }
}

void TCCPortalBridge::dispatch(const std::vector<pollfd> &fds, size_t first) {
  // Calls made while dispatching are added at the end and weren't polled.
  auto it = mCalls.begin();
  for (size_t i = first; i < fds.size() && it != mCalls.end(); i++, it++) {
    if (fds[i].revents)
      varlink_connection_process_events((*it)->conn, fds[i].revents);
  }

  for (auto it = mCalls.begin(); it != mCalls.end();) {
    Call *c = it->get();
    if (!c->done && !varlink_connection_is_closed(c->conn)) {
      it++;
      continue;
    }
    if (!c->done && c->on_lost)
      c->on_lost();
    it = mCalls.erase(it);
  }
}
