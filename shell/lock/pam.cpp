#include "pam.hpp"

#include "pam_loader.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

TCCPamAuth::TCCPamAuth() { mlock(mPassword, sizeof(mPassword)); }

TCCPamAuth::~TCCPamAuth() {
  if (mThread.joinable()) {
    mThread.join();
  }
  explicit_bzero(mPassword, sizeof(mPassword));
  munlock(mPassword, sizeof(mPassword));
}

// Answers every prompt PAM has with the password.
static int conversation(int num_msg, const struct pam_message **msg,
                        struct pam_response **resp, void *appdata_ptr) {
  const char *password = (const char *)appdata_ptr;

  pam_response *replies = (pam_response *)calloc(num_msg, sizeof(*replies));
  if (!replies) {
    return PAM_BUF_ERR;
  }

  for (int i = 0; i < num_msg; i++) {
    switch (msg[i]->msg_style) {
    case PAM_PROMPT_ECHO_OFF:
    case PAM_PROMPT_ECHO_ON:
      replies[i].resp = strdup(password);
      if (!replies[i].resp) {
        for (int j = 0; j < i; j++) {
          free(replies[j].resp);
        }
        free(replies);
        return PAM_BUF_ERR;
      }
      break;
    case PAM_ERROR_MSG:
    case PAM_TEXT_INFO:
      fprintf(stderr, "pam: %s\n", msg[i]->msg);
      break;
    }
  }

  *resp = replies;
  return PAM_SUCCESS;
}

void TCCPamAuth::start(const std::string &user, const char *password,
                       size_t len, int wake_fd) {
  if (mBusy) {
    return;
  }
  if (len >= sizeof(mPassword)) {
    len = sizeof(mPassword) - 1;
  }
  memcpy(mPassword, password, len);
  mPassword[len] = '\0';

  mBusy = true;
  mDone = false;
  mThread = std::thread([this, user, wake_fd]() {
    pam_handle_t *handle = nullptr;
    const pam_conv conv = {conversation, mPassword};

    int status = pam_start(TCC_PAM_SERVICE, user.c_str(), &conv, &handle);
    if (status == PAM_SUCCESS) {
      status = pam_authenticate(handle, 0);
    }
    snprintf(mMessage, sizeof(mMessage), "%s", pam_strerror(handle, status));
    if (handle) {
      pam_end(handle, status);
    }

    explicit_bzero(mPassword, sizeof(mPassword));
    mStatus = status;
    mDone.store(true, std::memory_order_release);

    uint64_t one = 1;
    if (write(wake_fd, &one, sizeof(one)) < 0) {
      perror("write");
    }
  });
}

bool TCCPamAuth::poll_result(bool *ok, std::string *message) {
  if (!mBusy || !mDone.load(std::memory_order_acquire)) {
    return false;
  }
  mThread.join();
  mBusy = false;

  *ok = mStatus == PAM_SUCCESS;
  if (mStatus == PAM_AUTH_ERR) {
    *message = "Incorrect password.";
  } else {
    *message = mMessage;
  }
  return true;
}
