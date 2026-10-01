#include "portal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

void TCCPortalSubscribers::closed(VarlinkCall *call, void *userdata) {
  TCCPortalSubscribers *subscribers = (TCCPortalSubscribers *)userdata;
  auto &calls = subscribers->mCalls;
  auto it = std::find(calls.begin(), calls.end(), call);
  if (call && it != calls.end()) {
    calls.erase(it);
    varlink_call_unref(call);
  }
}

long TCCPortalSubscribers::subscribe(VarlinkService *service, VarlinkCall *call,
                                     VarlinkObject *parameters, uint64_t flags,
                                     void *userdata) {
  if (!(flags & VARLINK_CALL_MORE))
    return varlink_call_reply_error(call, "org.varlink.service.ExpectedMore",
                                    nullptr);
  TCCPortalSubscribers *subscribers = (TCCPortalSubscribers *)userdata;
  subscribers->mCalls.push_back(varlink_call_ref(call));
  varlink_call_set_connection_closed_callback(call, closed, subscribers);
  return 0;
}

void TCCPortalSubscribers::emit(const char *name, VarlinkObject *args) {
  VarlinkObject *reply = nullptr;
  if (varlink_object_new(&reply) != 0)
    return;
  varlink_object_set_object(reply, name, args);
  for (VarlinkCall *call : mCalls)
    varlink_call_reply(call, reply, VARLINK_REPLY_CONTINUES);
  varlink_object_unref(reply);
}

TCCPortal &TCCPortal::get() {
  static TCCPortal portal;
  return portal;
}

TCCPortal::~TCCPortal() {
  if (mService)
    varlink_service_free(mService);
}

long TCCPortal::noop(VarlinkCall *call, const char *reply) {
  VarlinkObject *parameters = nullptr;
  long ret;
  if (reply[0] == '{') {
    if (varlink_object_new_from_json(&parameters, reply) != 0) {
      fprintf(stderr, "tcc_portal: bad no-op reply for %s\n",
              varlink_call_get_method(call));
      return varlink_call_reply_error(
          call, "org.varlink.service.MethodNotImplemented", nullptr);
    }
    ret = varlink_call_reply(call, parameters, 0);
  } else {
    // The method's own interface.
    std::string method = varlink_call_get_method(call);
    std::string error = method.substr(0, method.rfind('.') + 1) + reply;
    varlink_object_new(&parameters);
    varlink_object_set_string(parameters, "message", "Not implemented");
    ret = varlink_call_reply_error(call, error.c_str(), parameters);
  }
  varlink_object_unref(parameters);
  return ret;
}

bool TCCPortal::checkInterface(const char *name, long error) {
  if (error != 0)
    fprintf(stderr, "tcc_portal: could not add the %s interface: %s\n", name,
            varlink_error_string(-error));
  return error == 0;
}

bool TCCPortal::start(const char *address) {
  long error = varlink_service_new(&mService, "TCC", "Portal Service", "1",
                                   "https://ioi-xd.net", address, -1);
  if (error != 0) {
    fprintf(stderr, "tcc_portal: could not listen on %s: %s\n", address,
            varlink_error_string(-error));
    mService = nullptr;
    return false;
  }
  mRegistry = new TCCRegistryConnection();
  mRegistrySubscription = new TCCRegistrySubscription(
      [this](const std::string &key, const TCCRegistryValue &value) {
        if (key == "Dark Theme" && std::holds_alternative<bool>(value))
          updateDarkTheme(std::get<bool>(value));
      });
  mDarkTheme = mRegistry->GetValue<bool>("Dark Theme");

  return addInhibit() && addNotification() && addRequest() && addSession() &&
         addSettings();
}

int TCCPortal::fd() { return varlink_service_get_fd(mService); }

void TCCPortal::process() {
  long error = varlink_service_process_events(mService);
  if (error < 0)
    fprintf(stderr, "tcc_portal: varlink service: %s\n",
            varlink_error_string(-error));
}

bool TCCPortal::subscribeRegistry() {
  if (mRegistrySubscription->subscribed())
    return true;
  if (!mRegistrySubscription->subscribe())
    return false;
  // It may have changed while we weren't subscribed.
  updateDarkTheme(mRegistry->GetValue<bool>("Dark Theme"));
  return true;
}

int TCCPortal::registryFd() { return mRegistrySubscription->fd(); }

short TCCPortal::registryEvents() { return mRegistrySubscription->events(); }

void TCCPortal::processRegistry(short revents) {
  mRegistrySubscription->process(revents);
}
