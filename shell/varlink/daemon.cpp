#include "../client/client.hpp"

#include "daemon.hpp"
#include "interface.varlink.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <sys/epoll.h>

TCCRegistryDaemon::TCCRegistryDaemon(TCCClient *cli) : mClient(cli) {
  int error;

  if ((error = varlink_service_new(&mService, "TCC", "Registry Service", "1",
                                   "https://ioi-xd.net",
                                   "unix:@tcc_registry.socket", -1)) != 0) {
    printf("varlink_service_new error: %s\n", varlink_error_string(-error));
    raise(SIGTRAP);
  };
  VarlinkCall *later_call = NULL;
  if ((error = varlink_service_add_interface(
           mService, VARLINK_INTERFACE_SOURCE,   //
           "InitiateClose", InitiateClose, this, /* InitiateClose */
           NULL                                  //
           )) != 0) {
    printf("varlink_service_add_interface error: %s\n",
           varlink_error_string(-error));
    raise(SIGTRAP);
  };
}
void TCCRegistryDaemon::run() {
  mEPollFD = epoll_create1(EPOLL_CLOEXEC);
  if (mEPollFD < 0) {
    perror("epoll_create1");
    raise(SIGTRAP);
    return;
  }

  epoll_event ev = {};
  ev.events = EPOLLIN;
  ev.data.fd = varlink_service_get_fd(mService);
  if (epoll_ctl(mEPollFD, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0) {
    perror("epoll_ctl");
    raise(SIGTRAP);
    return;
  }

  for (;;) {
    epoll_event events[8];
    int n = epoll_wait(mEPollFD, events, 8, -1);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("epoll_wait");
      break;
    }

    for (int i = 0; i < n; i++) {
      if (events[i].data.fd == varlink_service_get_fd(mService)) {
        int error = 0;

        if ((error = varlink_service_process_events(mService)) < 0) {
          printf("varlink_service_process_events error: %s\n",
                 varlink_error_string(-error));
        }
      }
    }
  }
}
