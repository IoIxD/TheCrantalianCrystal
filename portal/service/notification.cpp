/*
 * net.ioi-xd.tcc.portal.Notification. Not implemented yet.
 */
#include "portal.hpp"
#include "portal_varlink.h"

long TCCPortal::NotificationAddNotification(VarlinkService *service,
                                            VarlinkCall *call,
                                            VarlinkObject *parameters,
                                            uint64_t flags, void *userdata) {
  return noop(call, R"({})");
}

long TCCPortal::NotificationRemoveNotification(VarlinkService *service,
                                               VarlinkCall *call,
                                               VarlinkObject *parameters,
                                               uint64_t flags, void *userdata) {
  return noop(call, R"({})");
}

long TCCPortal::NotificationGetProperties(VarlinkService *service,
                                          VarlinkCall *call,
                                          VarlinkObject *parameters,
                                          uint64_t flags, void *userdata) {
  return noop(
      call,
      R"({"properties":{"SupportedOptions":{"signature":"a{sv}","value":{}},"version":{"signature":"u","value":1}}})");
}

bool TCCPortal::addNotification() {
  long error = varlink_service_add_interface(
      mService, VARLINK_Notification,                             //
      "AddNotification", NotificationAddNotification, this,       //
      "RemoveNotification", NotificationRemoveNotification, this, //
      "GetProperties", NotificationGetProperties, this,           //
      "Subscribe", TCCPortalSubscribers::subscribe,
      &mNotificationSubscribers, //
      nullptr);
  return checkInterface("Notification", error);
}
