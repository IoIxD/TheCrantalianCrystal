/*
 * net.ioi-xd.tcc.portal.Inhibit. Not implemented yet.
 */
#include "portal.hpp"
#include "portal_varlink.h"

long TCCPortal::InhibitInhibit(VarlinkService *service, VarlinkCall *call,
                               VarlinkObject *parameters, uint64_t flags,
                               void *userdata) {
  return noop(call, R"({})");
}

long TCCPortal::InhibitCreateMonitor(VarlinkService *service, VarlinkCall *call,
                                     VarlinkObject *parameters, uint64_t flags,
                                     void *userdata) {
  return noop(call, R"({"response":2})");
}

long TCCPortal::InhibitQueryEndResponse(VarlinkService *service,
                                        VarlinkCall *call,
                                        VarlinkObject *parameters,
                                        uint64_t flags, void *userdata) {
  return noop(call, R"({})");
}

bool TCCPortal::addInhibit() {
  long error = varlink_service_add_interface(
      mService, VARLINK_Inhibit,                                          //
      "Inhibit", InhibitInhibit, this,                                    //
      "CreateMonitor", InhibitCreateMonitor, this,                        //
      "QueryEndResponse", InhibitQueryEndResponse, this,                  //
      "Subscribe", TCCPortalSubscribers::subscribe, &mInhibitSubscribers, //
      nullptr);
  return checkInterface("Inhibit", error);
}
