#include "registry.hpp"
#include "dynload.hpp"
#include <csignal>
#include <cstdio>
#include <poll.h>

TCCRegistryConnection::TCCRegistryConnection() {
  int error = 0;
  if ((error = varlink_connection_new(&mConn, "unix:@tcc_registry.socket")) !=
      0) {
    printf("[connection] varlink_connection_new error: %s\n",
           varlink_error_string(-error));
    mValidConn = false;
    return;
  };
}

static long close_callback(VarlinkConnection *connection, const char *error,
                           VarlinkObject *parameters, uint64_t flags,
                           void *userdata) {
  printf("close callback\n");
  return 0;
}

void TCCRegistryConnection::InitiateClose() {
  if (!mValidConn) {
    return;
  }

  VarlinkObject *parameters = NULL;

  if (varlink_object_new(&parameters) != 0) {
    printf("varlink_object_new error\n");
    return;
  };
  if (varlink_connection_call(mConn, "net.ioi-xd.tccdaemon.InitiateClose",
                              parameters, 0, close_callback, this) != 0) {
    printf("varlink_connection_call error\n");
    return;
  };
  if (varlink_object_unref(parameters) != NULL) {
    printf("varlink_object_unref error\n");
    return;
  };
}

void TCCRegistryConnection::step() {
  if (!mValidConn) {
    return;
  }

  pollfd pfd = {};
  pfd.fd = varlink_connection_get_fd(mConn);
  pfd.events = varlink_connection_get_events(mConn);

  int ret = poll(&pfd, 1, 0);
  if (ret < 0) {
    printf("[connection] poll error\n");
    return;
  }
  if (ret == 0) {
    return;
  }

  long error = 0;
  if ((error = varlink_connection_process_events(mConn, pfd.revents)) != 0) {
    printf("[connection] varlink_connection_process_events error: %ld\n",
           error);
    return;
  };
}

void __attribute__((constructor)) setup(void) {
  if (!dynload_setup::varlink()) {
    raise(SIGTRAP);
  }
}
