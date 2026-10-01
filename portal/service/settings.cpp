/*
 * net.ioi-xd.tcc.portal.Settings.
 *
 * NOTE: since dbus isn't used internally the only thing we actually care about
 * returning is the color scheme.
 */
#include "portal.hpp"
#include "portal_varlink.h"

#include <cstring>

// The color scheme as a Variant: 1 for dark, 2 for light.
static VarlinkObject *color_scheme_variant(bool dark) {
  VarlinkObject *variant = nullptr;
  if (varlink_object_new(&variant) != 0)
    return nullptr;
  varlink_object_set_string(variant, "signature", "u");
  varlink_object_set_int(variant, "value", dark ? 1 : 2);
  return variant;
}

// The same from tcc_registry, or nullptr if it isn't running.
static VarlinkObject *color_scheme(TCCRegistryConnection *registry) {
  auto dark = registry->GetValue<bool>("Dark Theme");
  return dark ? color_scheme_variant(*dark) : nullptr;
}

// Whether `ns` matches one of `patterns`: the namespace itself, or a prefix of
// it ending in "*". No patterns (or an empty one) matches everything.
static bool namespace_matches(const std::string &ns,
                              const std::vector<std::string> &patterns) {
  if (patterns.empty())
    return true;
  for (const std::string &pattern : patterns) {
    if (pattern.empty() || pattern == ns)
      return true;
    if (pattern.back() == '*' &&
        ns.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0)
      return true;
  }
  return false;
}

long TCCPortal::SettingsReadAll(VarlinkService *service, VarlinkCall *call,
                                VarlinkObject *parameters, uint64_t flags,
                                void *userdata) {
  TCCPortal *portal = (TCCPortal *)userdata;

  std::vector<std::string> patterns;
  VarlinkArray *namespaces = nullptr;
  if (varlink_object_get_array(parameters, "namespaces", &namespaces) != 0)
    return varlink_call_reply_invalid_parameter(call, "namespaces");
  for (long i = 0; i < varlink_array_get_n_elements(namespaces); i++) {
    const char *pattern = nullptr;
    if (varlink_array_get_string(namespaces, i, &pattern) == 0)
      patterns.push_back(pattern);
  }

  // Left empty if the namespace isn't asked for or there's no color scheme.
  VarlinkObject *value = nullptr;
  varlink_object_new(&value);
  VarlinkObject *variant = nullptr;
  if (namespace_matches("org.freedesktop.appearance", patterns) &&
      (variant = color_scheme(portal->mRegistry))) {
    VarlinkObject *appearance = nullptr;
    varlink_object_new(&appearance);
    varlink_object_set_object(appearance, "color-scheme", variant);
    varlink_object_set_object(value, "org.freedesktop.appearance", appearance);
    varlink_object_unref(appearance);
    varlink_object_unref(variant);
  }

  VarlinkObject *reply = nullptr;
  varlink_object_new(&reply);
  varlink_object_set_object(reply, "value", value);
  varlink_object_unref(value);

  long ret = varlink_call_reply(call, reply, 0);
  varlink_object_unref(reply);
  return ret;
}

long TCCPortal::SettingsRead(VarlinkService *service, VarlinkCall *call,
                             VarlinkObject *parameters, uint64_t flags,
                             void *userdata) {
  TCCPortal *portal = (TCCPortal *)userdata;
  const char *ns = nullptr;
  const char *key = nullptr;
  if (varlink_object_get_string(parameters, "namespace", &ns) != 0)
    return varlink_call_reply_invalid_parameter(call, "namespace");
  if (varlink_object_get_string(parameters, "key", &key) != 0)
    return varlink_call_reply_invalid_parameter(call, "key");

  VarlinkObject *variant = nullptr;
  if (strcmp(ns, "org.freedesktop.appearance") == 0 &&
      strcmp(key, "color-scheme") == 0 &&
      (variant = color_scheme(portal->mRegistry))) {
    VarlinkObject *reply = nullptr;
    varlink_object_new(&reply);
    varlink_object_set_object(reply, "value", variant);
    varlink_object_unref(variant);

    long ret = varlink_call_reply(call, reply, 0);
    varlink_object_unref(reply);
    return ret;
  }

  VarlinkObject *error = nullptr;
  varlink_object_new(&error);
  varlink_object_set_string(error, "message", "Requested setting not found");
  long ret = varlink_call_reply_error(
      call, "net.ioi-xd.tcc.portal.Settings.NotFound", error);
  varlink_object_unref(error);
  return ret;
}

long TCCPortal::SettingsGetProperties(VarlinkService *service,
                                      VarlinkCall *call,
                                      VarlinkObject *parameters, uint64_t flags,
                                      void *userdata) {
  // org.freedesktop.impl.portal.Settings has the one property.
  VarlinkObject *version = nullptr;
  varlink_object_new(&version);
  varlink_object_set_string(version, "signature", "u");
  varlink_object_set_int(version, "value", 1);

  VarlinkObject *properties = nullptr;
  varlink_object_new(&properties);
  varlink_object_set_object(properties, "version", version);
  varlink_object_unref(version);

  VarlinkObject *reply = nullptr;
  varlink_object_new(&reply);
  varlink_object_set_object(reply, "properties", properties);
  varlink_object_unref(properties);

  long ret = varlink_call_reply(call, reply, 0);
  varlink_object_unref(reply);
  return ret;
}

void TCCPortal::updateDarkTheme(std::optional<bool> dark) {
  if (dark == mDarkTheme)
    return;
  mDarkTheme = dark;
  if (!dark)
    return;

  VarlinkObject *variant = color_scheme_variant(*dark);
  VarlinkObject *args = nullptr;
  if (!variant || varlink_object_new(&args) != 0) {
    if (variant)
      varlink_object_unref(variant);
    return;
  }
  varlink_object_set_string(args, "namespace", "org.freedesktop.appearance");
  varlink_object_set_string(args, "key", "color-scheme");
  varlink_object_set_object(args, "value", variant);
  varlink_object_unref(variant);

  mSettingsSubscribers.emit("SettingChanged", args);
  varlink_object_unref(args);
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
