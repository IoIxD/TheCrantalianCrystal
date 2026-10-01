#include "session_env.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

static const char *DESKTOP_NAME = "TCC";

#ifdef TCC_HAS_DBUS
#include "dbus_loader.hpp"

// So a hung bus can't hang the shell on startup.
static const int CALL_TIMEOUT_MS = 1000;

static void call(DBusConnection *conn, DBusMessage *msg, const char *what) {
  DBusError err;
  dbus_error_init(&err);
  DBusMessage *reply = dbus_connection_send_with_reply_and_block(
      conn, msg, CALL_TIMEOUT_MS, &err);
  dbus_message_unref(msg);
  if (!reply) {
    fprintf(stderr, "session env: could not update the %s environment: %s\n",
            what, err.message);
    dbus_error_free(&err);
    return;
  }
  dbus_message_unref(reply);
}

static void update_activation_env(const char *name, const char *value) {
  if (!DBUS_LIB || !DBUS_LIB->handle) {
    return;
  }

  DBusError err;
  dbus_error_init(&err);
  DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
  if (!conn) {
    fprintf(stderr, "session env: could not connect to the session bus: %s\n",
            err.message);
    dbus_error_free(&err);
    return;
  }
  dbus_connection_set_exit_on_disconnect(conn, false);

  // dbus-daemon/dbus-broker: UpdateActivationEnvironment(a{ss})
  DBusMessage *msg = dbus_message_new_method_call(
      "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
      "UpdateActivationEnvironment");
  DBusMessageIter iter, dict, entry;
  dbus_message_iter_init_append(msg, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{ss}", &dict);
  dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &value);
  dbus_message_iter_close_container(&dict, &entry);
  dbus_message_iter_close_container(&iter, &dict);
  call(conn, msg, "D-Bus activation");

  // systemd --user: SetEnvironment(as). xdg-desktop-portal is usually started
  // as a user unit, which doesn't see the D-Bus one.
  std::string assignment = std::string(name) + "=" + value;
  const char *assignment_str = assignment.c_str();
  msg = dbus_message_new_method_call(
      "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
      "org.freedesktop.systemd1.Manager", "SetEnvironment");
  DBusMessageIter array;
  dbus_message_iter_init_append(msg, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &array);
  dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &assignment_str);
  dbus_message_iter_close_container(&iter, &array);
  call(conn, msg, "systemd user");

  dbus_connection_close(conn);
  dbus_connection_unref(conn);
}
#else
static void update_activation_env(const char *, const char *) {}
#endif

void tcc_setup_session_env() {
  setenv("XDG_CURRENT_DESKTOP", DESKTOP_NAME, 1);
  update_activation_env("XDG_CURRENT_DESKTOP", DESKTOP_NAME);
}
