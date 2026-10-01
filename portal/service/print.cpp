/*
 * net.ioi-xd.tcc.portal.Print. Not implemented yet.
 */
#include "portal.hpp"
#include "portal_varlink.h"

long TCCPortal::PrintPreparePrint(VarlinkService *service, VarlinkCall *call,
                                  VarlinkObject *parameters, uint64_t flags,
                                  void *userdata) {
  return noop(call, R"({"response":2,"results":{}})");
}

long TCCPortal::PrintPrint(VarlinkService *service, VarlinkCall *call,
                           VarlinkObject *parameters, uint64_t flags,
                           void *userdata) {
  return noop(call, R"({"response":2,"results":{}})");
}

bool TCCPortal::addPrint() {
  long error =
      varlink_service_add_interface(mService, VARLINK_Print,                 //
                                    "PreparePrint", PrintPreparePrint, this, //
                                    "Print", PrintPrint, this,               //
                                    nullptr);
  return checkInterface("Print", error);
}
