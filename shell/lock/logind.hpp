#pragma once

#include <functional>
#include <string>

struct DBusConnection;

/*
 * Our session in systemd-logind: listens for Lock/Unlock (loginctl
 * lock-session and friends) and PrepareForSleep, tells it whether we're
 * locked, and holds a delay inhibitor so we can lock before the system
 * sleeps.
 *
 * Does nothing if DBus or logind aren't available.
 */
class TCCLogind {
  DBusConnection *mConn = nullptr;
  std::string mSessionPath;
  int mInhibitFd = -1;

  bool find_session();
  void close();

public:
  std::function<void()> on_lock;
  std::function<void()> on_unlock;
  std::function<void(bool start)> on_prepare_for_sleep;

  ~TCCLogind();

  bool connect();
  // -1 if not connected.
  int fd() const;
  void process();

  void set_locked_hint(bool locked);
  void take_sleep_inhibitor();
  void release_sleep_inhibitor();
};
