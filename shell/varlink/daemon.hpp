#pragma once

// #define TCC_DYNLOAD_SKIP_DEFINES
#include "varlink_loader.hpp"
#include <thread>

#include "wayland_loader.h"

class TCCRegistryDaemon {
  class TCCClient *mClient;
  VarlinkService *mService;
  int mEPollFD = -1;
  std::thread *mThread;

  static void InitiateClose(VarlinkService *service, VarlinkCall *call,
                            VarlinkObject *parameters, uint64_t flags,
                            void *userdata);

public:
  TCCRegistryDaemon(class TCCClient *cli);
  void run();
};
