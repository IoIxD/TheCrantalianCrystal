#include "../client/client.hpp"
#include "daemon.hpp"
#include <Mw/Milsko.h>
#include <cstdio>
#include <thread>

void TCCClientVarlink::InitiateClose(VarlinkService *service, VarlinkCall *call,
                                     VarlinkObject *parameters, uint64_t flags,
                                     void *userdata) {
  TCCClientVarlink *d = (TCCClientVarlink *)userdata;
  printf("close initiated\n");
  varlink_call_reply(call, NULL, 0);

  d->mClient->terminate();
};
void TCCClientVarlink::LockScreen(VarlinkService *service, VarlinkCall *call,
                                  VarlinkObject *parameters, uint64_t flags,
                                  void *userdata) {
  TCCClientVarlink *d = (TCCClientVarlink *)userdata;
  printf("lock initiate\n");
  varlink_call_reply(call, NULL, 0);

  d->mClient->lock();
};
