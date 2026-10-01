/*
 * net.ioi-xd.tcc.portal.Request. Not implemented yet.
 */
#include "portal.hpp"
#include "portal_varlink.h"

long TCCPortal::RequestClose(VarlinkService *service, VarlinkCall *call,
                             VarlinkObject *parameters, uint64_t flags,
                             void *userdata) {
  return noop(call, R"({})");
}

bool TCCPortal::addRequest() {
  long error = varlink_service_add_interface(mService, VARLINK_Request,   //
                                             "Close", RequestClose, this, //
                                             nullptr);
  return checkInterface("Request", error);
}
