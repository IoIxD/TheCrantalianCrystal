/*
 * net.ioi-xd.tcc.portal.Account. Not implemented yet.
 */
#include "portal.hpp"
#include "portal_varlink.h"

long TCCPortal::AccountGetUserInformation(VarlinkService *service,
                                          VarlinkCall *call,
                                          VarlinkObject *parameters,
                                          uint64_t flags, void *userdata) {
  return noop(call, R"({"response":2,"results":{}})");
}

bool TCCPortal::addAccount() {
  long error = varlink_service_add_interface(mService, VARLINK_Account, //
                                             "GetUserInformation",
                                             AccountGetUserInformation, this, //
                                             nullptr);
  return checkInterface("Account", error);
}
