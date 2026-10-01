/*
 * net.ioi-xd.tcc.portal.Email. Not implemented yet.
 */
#include "portal.hpp"
#include "portal_varlink.h"

long TCCPortal::EmailComposeEmail(VarlinkService *service, VarlinkCall *call,
                                  VarlinkObject *parameters, uint64_t flags,
                                  void *userdata) {
  return noop(call, R"({"response":2,"results":{}})");
}

bool TCCPortal::addEmail() {
  long error =
      varlink_service_add_interface(mService, VARLINK_Email,                 //
                                    "ComposeEmail", EmailComposeEmail, this, //
                                    nullptr);
  return checkInterface("Email", error);
}
