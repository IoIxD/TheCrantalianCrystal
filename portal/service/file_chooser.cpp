/*
 * net.ioi-xd.tcc.portal.FileChooser. Not implemented yet.
 */
#include "portal.hpp"
#include "portal_varlink.h"

long TCCPortal::FileChooserOpenFile(VarlinkService *service, VarlinkCall *call,
                                    VarlinkObject *parameters, uint64_t flags,
                                    void *userdata) {
  return noop(call, R"({"response":2,"results":{}})");
}

long TCCPortal::FileChooserSaveFile(VarlinkService *service, VarlinkCall *call,
                                    VarlinkObject *parameters, uint64_t flags,
                                    void *userdata) {
  return noop(call, R"({"response":2,"results":{}})");
}

long TCCPortal::FileChooserSaveFiles(VarlinkService *service, VarlinkCall *call,
                                     VarlinkObject *parameters, uint64_t flags,
                                     void *userdata) {
  return noop(call, R"({"response":2,"results":{}})");
}

bool TCCPortal::addFileChooser() {
  long error =
      varlink_service_add_interface(mService, VARLINK_FileChooser,           //
                                    "OpenFile", FileChooserOpenFile, this,   //
                                    "SaveFile", FileChooserSaveFile, this,   //
                                    "SaveFiles", FileChooserSaveFiles, this, //
                                    nullptr);
  return checkInterface("FileChooser", error);
}
