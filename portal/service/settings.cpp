/*
 * net.ioi-xd.tcc.portal.Settings. Not implemented yet.
 */
#include "portal.hpp"
#include "portal_varlink.h"

long TCCPortal::SettingsReadAll(VarlinkService *service, VarlinkCall *call,
                                VarlinkObject *parameters, uint64_t flags,
                                void *userdata) {
  return noop(call, R"({"value":{}})");
}

long TCCPortal::SettingsRead(VarlinkService *service, VarlinkCall *call,
                             VarlinkObject *parameters, uint64_t flags,
                             void *userdata) {
  const char *_namespace = NULL;
  const char *key = NULL;
  varlink_object_get_string(parameters, "namespace", &_namespace);
  varlink_object_get_string(parameters, "key", &key);

  /* currently we only care to respond to appeareance */

  if (std::string(_namespace) == "org.freedesktop.appearance") {
    if (std::string(key) == "color-scheme") {
    }
  }

  // The method's own interface.
  std::string method = varlink_call_get_method(call);
  std::string error = method.substr(0, method.rfind('.') + 1) + reply;
  varlink_object_new(&parameters);
  varlink_object_set_string(parameters, "message", "Not implemented");
  ret = varlink_call_reply_error(call, error.c_str(), parameters);

  return noop(call, "NotFound");
}

long TCCPortal::SettingsGetProperties(VarlinkService *service,
                                      VarlinkCall *call,
                                      VarlinkObject *parameters, uint64_t flags,
                                      void *userdata) {
  return noop(call,
              R"({"properties":{"version":{"signature":"u","value":1}}})");
}

bool TCCPortal::addSettings() {
  long error = varlink_service_add_interface(
      mService, VARLINK_Settings,                                          //
      "ReadAll", SettingsReadAll, this,                                    //
      "Read", SettingsRead, this,                                          //
      "GetProperties", SettingsGetProperties, this,                        //
      "Subscribe", TCCPortalSubscribers::subscribe, &mSettingsSubscribers, //
      nullptr);
  return checkInterface("Settings", error);
}
