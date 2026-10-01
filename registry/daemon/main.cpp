/*
 * tcc_registry: the settings daemon. Serves net.ioi-xd.tcc.registry on
 * $XDG_RUNTIME_DIR/tcc-registry.varlink, keeping values in
 * $XDG_CONFIG_HOME/tcc/settings.db.
 */
#include "dynload.hpp"
#include "keys.hpp"
#include "registry_interface.h"
#include "store.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

// Calls to Subscribe, kept open to reply to on every change.
static std::vector<VarlinkCall *> subscribers;

static long reply_error(VarlinkCall *call, const char *error,
                        VarlinkObject *parameters) {
  long ret = varlink_call_reply_error(
      call, (std::string(TCC_REGISTRY_INTERFACE ".") + error).c_str(),
      parameters);
  varlink_object_unref(parameters);
  return ret;
}

static long reply_invalid_key(VarlinkCall *call, const char *key) {
  VarlinkObject *parameters = nullptr;
  varlink_object_new(&parameters);
  varlink_object_set_string(parameters, "key", key);
  return reply_error(call, "InvalidKey", parameters);
}

static long reply_failed(VarlinkCall *call, const char *message) {
  VarlinkObject *parameters = nullptr;
  varlink_object_new(&parameters);
  varlink_object_set_string(parameters, "message", message);
  return reply_error(call, "Failed", parameters);
}

static long GetValue(VarlinkService *service, VarlinkCall *call,
                     VarlinkObject *parameters, uint64_t flags,
                     void *userdata) {
  auto *store = static_cast<TCCRegistryStore *>(userdata);

  const char *name = nullptr;
  if (varlink_object_get_string(parameters, "key", &name) != 0)
    return varlink_call_reply_invalid_parameter(call, "key");
  const TCCRegistryKey *key = tcc_registry_find_key(name);
  if (!key)
    return reply_invalid_key(call, name);

  TCCRegistryValue value = store->get(key->name, key->type).value_or(key->def);
  VarlinkObject *object = tcc_registry_value_to_varlink(value);
  VarlinkObject *out = nullptr;
  if (!object || varlink_object_new(&out) != 0) {
    if (object)
      varlink_object_unref(object);
    return reply_failed(call, "Out of memory");
  }
  varlink_object_set_object(out, "value", object);
  varlink_object_unref(object);

  long ret = varlink_call_reply(call, out, 0);
  varlink_object_unref(out);
  return ret;
}

// Tells every subscriber `key` is now `value`.
static void notify(const std::string &key, const TCCRegistryValue &value) {
  VarlinkObject *object = tcc_registry_value_to_varlink(value);
  VarlinkObject *reply = nullptr;
  if (!object || varlink_object_new(&reply) != 0) {
    if (object)
      varlink_object_unref(object);
    return;
  }
  varlink_object_set_string(reply, "key", key.c_str());
  varlink_object_set_object(reply, "value", object);
  varlink_object_unref(object);

  for (VarlinkCall *call : subscribers)
    varlink_call_reply(call, reply, VARLINK_REPLY_CONTINUES);
  varlink_object_unref(reply);
}

static long SetValue(VarlinkService *service, VarlinkCall *call,
                     VarlinkObject *parameters, uint64_t flags,
                     void *userdata) {
  auto *store = static_cast<TCCRegistryStore *>(userdata);

  const char *name = nullptr;
  VarlinkObject *object = nullptr;
  if (varlink_object_get_string(parameters, "key", &name) != 0)
    return varlink_call_reply_invalid_parameter(call, "key");
  if (varlink_object_get_object(parameters, "value", &object) != 0)
    return varlink_call_reply_invalid_parameter(call, "value");
  const TCCRegistryKey *key = tcc_registry_find_key(name);
  if (!key)
    return reply_invalid_key(call, name);

  auto value = tcc_registry_value_from_varlink(object);
  if (!value || tcc_registry_type_of(*value) != key->type) {
    VarlinkObject *error = nullptr;
    varlink_object_new(&error);
    varlink_object_set_string(error, "key", name);
    varlink_object_set_string(error, "expected",
                              tcc_registry_type_name(key->type));
    return reply_error(call, "InvalidValue", error);
  }

  bool changed = store->get(key->name, key->type).value_or(key->def) != *value;
  if (!store->set(key->name, *value))
    return reply_failed(call, "Could not write to the database");
  long ret = varlink_call_reply(call, nullptr, 0);
  if (changed)
    notify(key->name, *value);
  return ret;
}

static void subscriber_closed(VarlinkCall *call, void *userdata) {
  auto it = std::find(subscribers.begin(), subscribers.end(), call);
  if (it != subscribers.end()) {
    subscribers.erase(it);
    varlink_call_unref(call);
  }
}

static long Subscribe(VarlinkService *service, VarlinkCall *call,
                      VarlinkObject *parameters, uint64_t flags,
                      void *userdata) {
  if (!(flags & VARLINK_CALL_MORE))
    return varlink_call_reply_error(call, "org.varlink.service.ExpectedMore",
                                    nullptr);
  subscribers.push_back(varlink_call_ref(call));
  varlink_call_set_connection_closed_callback(call, subscriber_closed,
                                              nullptr);
  return 0;
}

static long ListKeys(VarlinkService *service, VarlinkCall *call,
                     VarlinkObject *parameters, uint64_t flags,
                     void *userdata) {
  VarlinkArray *keys = nullptr;
  if (varlink_array_new(&keys) != 0)
    return reply_failed(call, "Out of memory");

  for (const TCCRegistryKey &key : tcc_registry_keys()) {
    VarlinkObject *object = nullptr;
    VarlinkObject *def = tcc_registry_value_to_varlink(key.def);
    if (!def || varlink_object_new(&object) != 0) {
      if (def)
        varlink_object_unref(def);
      varlink_array_unref(keys);
      return reply_failed(call, "Out of memory");
    }
    varlink_object_set_string(object, "name", key.name.c_str());
    varlink_object_set_string(object, "type", tcc_registry_type_name(key.type));
    varlink_object_set_object(object, "default", def);
    varlink_object_set_string(object, "description", key.description.c_str());
    varlink_array_append_object(keys, object);
    varlink_object_unref(def);
    varlink_object_unref(object);
  }

  VarlinkObject *out = nullptr;
  if (varlink_object_new(&out) != 0) {
    varlink_array_unref(keys);
    return reply_failed(call, "Out of memory");
  }
  varlink_object_set_array(out, "keys", keys);
  varlink_array_unref(keys);

  long ret = varlink_call_reply(call, out, 0);
  varlink_object_unref(out);
  return ret;
}

static std::filesystem::path config_dir() {
  const char *config_home = getenv("XDG_CONFIG_HOME");
  if (config_home && *config_home)
    return std::filesystem::path(config_home) / "tcc";
  const char *home = getenv("HOME");
  return std::filesystem::path(home ? home : "") / ".config" / "tcc";
}

int main() {
  if (!dynload_setup::varlink() || !dynload_setup::sqlite())
    return 1;

  // libvarlink write()s, so a client hanging up before its reply would
  // otherwise kill us.
  signal(SIGPIPE, SIG_IGN);

  const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (!runtime_dir || !*runtime_dir) {
    fprintf(stderr, "tcc_registry: XDG_RUNTIME_DIR isn't set\n");
    return 1;
  }
  std::string socket_path = std::string(runtime_dir) + "/tcc-registry.varlink";
  std::string lock_path = std::string(runtime_dir) + "/tcc-registry.lock";

  // Only one of us at a time; holding the lock means any socket is a leftover.
  int lock_fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
    if (errno == EWOULDBLOCK)
      fprintf(stderr, "tcc_registry: already running\n");
    else
      perror("tcc_registry: lock");
    return 1;
  }

  std::filesystem::path dir = config_dir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    fprintf(stderr, "tcc_registry: could not create %s: %s\n", dir.c_str(),
            ec.message().c_str());
    return 1;
  }

  TCCRegistryStore store;
  if (!store.open(dir / "settings.db"))
    return 1;

  unlink(socket_path.c_str());
  std::string address = "unix:" + socket_path;
  VarlinkService *service = nullptr;
  long error =
      varlink_service_new(&service, "TCC", "Registry", "1",
                          "https://ioi-xd.net", address.c_str(), -1);
  if (error != 0) {
    fprintf(stderr, "tcc_registry: varlink_service_new error: %s\n",
            varlink_error_string(-error));
    return 1;
  }

  error = varlink_service_add_interface(service, TCC_REGISTRY_INTERFACE_SOURCE,
                                        "GetValue", GetValue, &store,    //
                                        "SetValue", SetValue, &store,    //
                                        "ListKeys", ListKeys, nullptr,   //
                                        "Subscribe", Subscribe, nullptr, //
                                        nullptr);
  if (error != 0) {
    fprintf(stderr, "tcc_registry: varlink_service_add_interface error: %s\n",
            varlink_error_string(-error));
    return 1;
  }

  for (;;) {
    pollfd pfd = {varlink_service_get_fd(service), POLLIN, 0};
    if (poll(&pfd, 1, -1) < 0) {
      if (errno == EINTR)
        continue;
      perror("tcc_registry: poll");
      break;
    }

    error = varlink_service_process_events(service);
    if (error < 0)
      fprintf(stderr, "tcc_registry: varlink_service_process_events error: %s\n",
              varlink_error_string(-error));
  }

  varlink_service_free(service);
  unlink(socket_path.c_str());
  return 0;
}
