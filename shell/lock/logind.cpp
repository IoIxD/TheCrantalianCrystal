#include "logind.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

TCCLogind::~TCCLogind() {
  release_sleep_inhibitor();
  close();
}

#ifdef TCC_HAS_DBUS
#include "dbus_loader.hpp"

static const char *LOGIN1_NAME = "org.freedesktop.login1";
static const char *MANAGER_PATH = "/org/freedesktop/login1";
static const char *MANAGER_IFACE = "org.freedesktop.login1.Manager";
static const char *SESSION_IFACE = "org.freedesktop.login1.Session";

// For the calls we have to wait on, so a hung logind can't hang the shell.
static const int CALL_TIMEOUT_MS = 1000;

static DBusHandlerResult filter(DBusConnection *connection, DBusMessage *msg,
                                void *data) {
  TCCLogind *logind = (TCCLogind *)data;

  if (dbus_message_is_signal(msg, SESSION_IFACE, "Lock")) {
    if (logind->on_lock)
      logind->on_lock();
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (dbus_message_is_signal(msg, SESSION_IFACE, "Unlock")) {
    if (logind->on_unlock)
      logind->on_unlock();
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (dbus_message_is_signal(msg, MANAGER_IFACE, "PrepareForSleep")) {
    dbus_bool_t start = false;
    if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_BOOLEAN, &start,
                              DBUS_TYPE_INVALID) &&
        logind->on_prepare_for_sleep) {
      logind->on_prepare_for_sleep(start);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

bool TCCLogind::connect() {
  if (!DBUS_LIB || !DBUS_LIB->handle) {
    return false;
  }

  DBusError err;
  dbus_error_init(&err);
  mConn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
  if (!mConn) {
    fprintf(stderr, "logind: could not connect to the system bus: %s\n",
            err.message);
    dbus_error_free(&err);
    return false;
  }
  dbus_connection_set_exit_on_disconnect(mConn, false);

  if (!find_session()) {
    close();
    return false;
  }

  // Signals are only sent to us for the session we're in (by path), and
  // only logind itself can send them.
  std::string session_rule = std::string("type='signal',sender='") +
                             LOGIN1_NAME + "',interface='" + SESSION_IFACE +
                             "',path='" + mSessionPath + "'";
  std::string sleep_rule = std::string("type='signal',sender='") + LOGIN1_NAME +
                           "',interface='" + MANAGER_IFACE +
                           "',member='PrepareForSleep'";
  for (const std::string &rule : {session_rule, sleep_rule}) {
    dbus_bus_add_match(mConn, rule.c_str(), &err);
    if (dbus_error_is_set(&err)) {
      fprintf(stderr, "logind: could not add match %s: %s\n", rule.c_str(),
              err.message);
      dbus_error_free(&err);
      close();
      return false;
    }
  }

  dbus_connection_add_filter(mConn, filter, this, nullptr);
  return true;
}

bool TCCLogind::find_session() {
  DBusMessage *msg = nullptr;

  // The session we were started in, or failing that whichever one our
  // process is part of.
  const char *id = getenv("XDG_SESSION_ID");
  if (id && *id) {
    msg = dbus_message_new_method_call(LOGIN1_NAME, MANAGER_PATH, MANAGER_IFACE,
                                       "GetSession");
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID);
  } else {
    dbus_uint32_t pid = getpid();
    msg = dbus_message_new_method_call(LOGIN1_NAME, MANAGER_PATH, MANAGER_IFACE,
                                       "GetSessionByPID");
    dbus_message_append_args(msg, DBUS_TYPE_UINT32, &pid, DBUS_TYPE_INVALID);
  }

  DBusError err;
  dbus_error_init(&err);
  DBusMessage *reply = dbus_connection_send_with_reply_and_block(
      mConn, msg, CALL_TIMEOUT_MS, &err);
  dbus_message_unref(msg);
  if (!reply) {
    fprintf(stderr, "logind: could not find our session: %s\n", err.message);
    dbus_error_free(&err);
    return false;
  }

  const char *path = nullptr;
  if (!dbus_message_get_args(reply, &err, DBUS_TYPE_OBJECT_PATH, &path,
                             DBUS_TYPE_INVALID)) {
    fprintf(stderr, "logind: bad GetSession reply: %s\n", err.message);
    dbus_error_free(&err);
    dbus_message_unref(reply);
    return false;
  }
  mSessionPath = path;
  dbus_message_unref(reply);
  return true;
}

void TCCLogind::close() {
  if (!mConn) {
    return;
  }
  dbus_connection_close(mConn);
  dbus_connection_unref(mConn);
  mConn = nullptr;
}

int TCCLogind::fd() const {
  int fd = -1;
  if (mConn && !dbus_connection_get_unix_fd(mConn, &fd)) {
    return -1;
  }
  return fd;
}

void TCCLogind::process() {
  if (!mConn) {
    return;
  }
  dbus_connection_read_write(mConn, 0);
  while (dbus_connection_dispatch(mConn) == DBUS_DISPATCH_DATA_REMAINS) {
  }
}

void TCCLogind::set_locked_hint(bool locked) {
  if (!mConn) {
    return;
  }
  DBusMessage *msg = dbus_message_new_method_call(
      LOGIN1_NAME, mSessionPath.c_str(), SESSION_IFACE, "SetLockedHint");
  dbus_bool_t hint = locked;
  dbus_message_append_args(msg, DBUS_TYPE_BOOLEAN, &hint, DBUS_TYPE_INVALID);
  dbus_message_set_no_reply(msg, true);
  dbus_connection_send(mConn, msg, nullptr);
  // Might otherwise sit in the queue until logind next sends us something.
  dbus_connection_flush(mConn);
  dbus_message_unref(msg);
}

void TCCLogind::take_sleep_inhibitor() {
  if (!mConn || mInhibitFd >= 0) {
    return;
  }

  DBusMessage *msg = dbus_message_new_method_call(LOGIN1_NAME, MANAGER_PATH,
                                                  MANAGER_IFACE, "Inhibit");
  const char *what = "sleep";
  const char *who = "TheCrantalianCrystal";
  const char *why = "Lock the screen before sleeping";
  const char *mode = "delay";
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &what, DBUS_TYPE_STRING, &who,
                           DBUS_TYPE_STRING, &why, DBUS_TYPE_STRING, &mode,
                           DBUS_TYPE_INVALID);

  DBusError err;
  dbus_error_init(&err);
  DBusMessage *reply = dbus_connection_send_with_reply_and_block(
      mConn, msg, CALL_TIMEOUT_MS, &err);
  dbus_message_unref(msg);
  if (!reply) {
    fprintf(stderr, "logind: could not take the sleep inhibitor: %s\n",
            err.message);
    dbus_error_free(&err);
    return;
  }

  int fd = -1;
  if (!dbus_message_get_args(reply, &err, DBUS_TYPE_UNIX_FD, &fd,
                             DBUS_TYPE_INVALID)) {
    fprintf(stderr, "logind: bad Inhibit reply: %s\n", err.message);
    dbus_error_free(&err);
  }
  mInhibitFd = fd;
  dbus_message_unref(reply);
}

void TCCLogind::release_sleep_inhibitor() {
  if (mInhibitFd < 0) {
    return;
  }
  ::close(mInhibitFd);
  mInhibitFd = -1;
}

#else

bool TCCLogind::connect() { return false; }
void TCCLogind::close() {}
int TCCLogind::fd() const { return -1; }
void TCCLogind::process() {}
void TCCLogind::set_locked_hint(bool locked) {}
void TCCLogind::take_sleep_inhibitor() {}
void TCCLogind::release_sleep_inhibitor() {}

#endif
