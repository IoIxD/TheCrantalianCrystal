/*
 * net.ioi-xd.tcc.portal.Session. Not implemented yet.
 */
#include "portal.hpp"
#include "portal_varlink.h"

long TCCPortal::SessionClose(VarlinkService *service, VarlinkCall *call,
                             VarlinkObject *parameters, uint64_t flags,
                             void *userdata) {
  return noop(call, R"({})");
}

long TCCPortal::SessionGetProperties(VarlinkService *service, VarlinkCall *call,
                                     VarlinkObject *parameters, uint64_t flags,
                                     void *userdata) {
  return noop(call,
              R"({"properties":{"version":{"signature":"u","value":1}}})");
}

bool TCCPortal::addSession() {
  long error = varlink_service_add_interface(
      mService, VARLINK_Session,                                          //
      "Close", SessionClose, this,                                        //
      "GetProperties", SessionGetProperties, this,                        //
      "Subscribe", TCCPortalSubscribers::subscribe, &mSessionSubscribers, //
      nullptr);
  return checkInterface("Session", error);
}
