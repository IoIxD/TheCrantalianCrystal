#pragma once

// libpam's headers (security/pam_appl.h) aren't installed on most systems
// without a -devel package, so the little of Linux-PAM's ABI we need is
// declared here instead.

struct pam_handle;
typedef struct pam_handle pam_handle_t;

struct pam_message {
  int msg_style;
  const char *msg;
};

struct pam_response {
  char *resp;
  int resp_retcode;
};

struct pam_conv {
  int (*conv)(int num_msg, const struct pam_message **msg,
              struct pam_response **resp, void *appdata_ptr);
  void *appdata_ptr;
};

#define PAM_SUCCESS 0
#define PAM_BUF_ERR 5
#define PAM_AUTH_ERR 7
#define PAM_CONV_ERR 19

#define PAM_PROMPT_ECHO_OFF 1
#define PAM_PROMPT_ECHO_ON 2
#define PAM_ERROR_MSG 3
#define PAM_TEXT_INFO 4

extern "C" {
int pam_start(const char *service_name, const char *user,
              const struct pam_conv *pam_conversation, pam_handle_t **pamh);
int pam_authenticate(pam_handle_t *pamh, int flags);
int pam_end(pam_handle_t *pamh, int pam_status);
const char *pam_strerror(pam_handle_t *pamh, int errnum);
}

#define TCC_PAM_FUNCS(X)                                                       \
  X(pam_start)                                                                 \
  X(pam_authenticate)                                                          \
  X(pam_end)                                                                   \
  X(pam_strerror)

struct PamLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_PAM_FUNCS(X)
#undef X

  void *handle = nullptr;
};

extern PamLib *PAM_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define pam_start PAM_LIB->pam_start
#define pam_authenticate PAM_LIB->pam_authenticate
#define pam_end PAM_LIB->pam_end
#define pam_strerror PAM_LIB->pam_strerror
#endif
