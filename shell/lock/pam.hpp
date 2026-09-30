#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>

#define TCC_PASSWORD_MAX 1024
#define TCC_PAM_SERVICE "tcc-lock"

/*
 * Checks a password with PAM on a worker thread, since PAM can take a while
 * (pam_unix alone waits ~2 seconds after a wrong password).
 */
class TCCPamAuth {
  std::thread mThread;
  bool mBusy = false;
  std::atomic<bool> mDone = false;

  // Handed to the conversation function, locked into memory.
  char mPassword[TCC_PASSWORD_MAX];
  int mStatus = 0;
  char mMessage[256] = {};

public:
  TCCPamAuth();
  ~TCCPamAuth();

  bool busy() const { return mBusy; }
  // Writes to wake_fd (an eventfd) once done.
  void start(const std::string &user, const char *password, size_t len,
             int wake_fd);
  // Returns true once, when a started conversation has finished.
  bool poll_result(bool *ok, std::string *message);
};
