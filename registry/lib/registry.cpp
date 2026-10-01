#include "registry.hpp"
#include "dynload.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <poll.h>
#include <pthread.h>

static const int CALL_TIMEOUT_MS = 1000;

// libvarlink write()s, which raises SIGPIPE if tcc_registry went away. That's
// the process's to handle, so instead of ignoring it, it's blocked on this
// thread while one of ours is in flight and any it caused is dropped.
class SigpipeBlock {
  sigset_t mPipe;
  sigset_t mOldMask;
  bool mWasPending;

  static bool pending() {
    sigset_t set;
    sigpending(&set);
    return sigismember(&set, SIGPIPE) == 1;
  }

public:
  SigpipeBlock() {
    sigemptyset(&mPipe);
    sigaddset(&mPipe, SIGPIPE);
    mWasPending = pending();
    pthread_sigmask(SIG_BLOCK, &mPipe, &mOldMask);
  }
  ~SigpipeBlock() {
    if (!mWasPending && pending()) {
      timespec zero = {};
      sigtimedwait(&mPipe, nullptr, &zero);
    }
    pthread_sigmask(SIG_SETMASK, &mOldMask, nullptr);
  }
};

TCCRegistryConnection::~TCCRegistryConnection() { disconnect(); }

bool TCCRegistryConnection::open() {
  return (mConn && !varlink_connection_is_closed(mConn)) || connect();
}

bool TCCRegistryConnection::connect() {
  disconnect();

  std::string address = tcc_registry_address();
  if (address.empty()) {
    printf("[registry] XDG_RUNTIME_DIR isn't set\n");
    return false;
  }

  long error = varlink_connection_new(&mConn, address.c_str());
  if (error != 0) {
    printf("[registry] varlink_connection_new error: %s\n",
           varlink_error_string(-error));
    mConn = nullptr;
    return false;
  }
  return true;
}

void TCCRegistryConnection::disconnect() {
  if (mConn)
    mConn = varlink_connection_free(mConn);
}

long TCCRegistryConnection::reply(VarlinkConnection *connection,
                                  const char *error, VarlinkObject *parameters,
                                  uint64_t flags, void *userdata) {
  auto *self = static_cast<TCCRegistryConnection *>(userdata);
  if (error)
    self->mError = error;
  else if (self->mOnReply)
    self->mOnReply(parameters);
  self->mDone = true;
  return 0;
}

TCCRegistryConnection::Result
TCCRegistryConnection::attempt(const char *method, VarlinkObject *parameters) {
  if (!open())
    return Result::Failed;

  mDone = false;
  long error =
      varlink_connection_call(mConn, method, parameters, 0, reply, this);
  if (error != 0) {
    disconnect();
    return Result::Lost;
  }

  while (!mDone) {
    pollfd pfd = {};
    pfd.fd = varlink_connection_get_fd(mConn);
    pfd.events = varlink_connection_get_events(mConn);

    int ret = poll(&pfd, 1, CALL_TIMEOUT_MS);
    if (ret < 0 && errno == EINTR)
      continue;
    if (ret <= 0) {
      printf("[registry] %s: %s\n", method,
             ret == 0 ? "timed out" : "poll error");
      // A late reply would come in for the next call instead.
      disconnect();
      return Result::Failed;
    }

    error = varlink_connection_process_events(mConn, pfd.revents);
    if (error != 0 && !mDone) {
      disconnect();
      return Result::Lost;
    }
  }
  return Result::Done;
}

bool TCCRegistryConnection::call(const char *method, VarlinkObject *parameters,
                                 std::function<void(VarlinkObject *)> on_reply) {
  mError.clear();
  if (!parameters)
    return false;
  SigpipeBlock sigpipe_block;
  mOnReply = std::move(on_reply);

  // If tcc_registry restarted, the connection we had only turns out to be
  // dead once it's used; then it's worth another go on a new one.
  bool had_connection = mConn && !varlink_connection_is_closed(mConn);
  Result result = attempt(method, parameters);
  if (result == Result::Lost && had_connection)
    result = attempt(method, parameters);
  varlink_object_unref(parameters);

  if (result == Result::Lost)
    printf("[registry] %s: lost the connection\n", method);
  if (result != Result::Done)
    return false;
  if (!mError.empty()) {
    printf("[registry] %s: %s\n", method, mError.c_str());
    return false;
  }
  return true;
}

std::optional<TCCRegistryValue>
TCCRegistryConnection::GetValue(const std::string &key) {
  VarlinkObject *parameters = nullptr;
  if (varlink_object_new(&parameters) != 0)
    return std::nullopt;
  varlink_object_set_string(parameters, "key", key.c_str());

  std::optional<TCCRegistryValue> value;
  call(TCC_REGISTRY_INTERFACE ".GetValue", parameters,
       [&](VarlinkObject *reply) {
         VarlinkObject *object = nullptr;
         if (varlink_object_get_object(reply, "value", &object) == 0)
           value = tcc_registry_value_from_varlink(object);
       });
  return value;
}

bool TCCRegistryConnection::SetValue(const std::string &key,
                                     const TCCRegistryValue &value) {
  VarlinkObject *object = tcc_registry_value_to_varlink(value);
  if (!object)
    return false;

  VarlinkObject *parameters = nullptr;
  if (varlink_object_new(&parameters) != 0) {
    varlink_object_unref(object);
    return false;
  }
  varlink_object_set_string(parameters, "key", key.c_str());
  varlink_object_set_object(parameters, "value", object);
  varlink_object_unref(object);

  return call(TCC_REGISTRY_INTERFACE ".SetValue", parameters);
}

std::optional<std::vector<TCCRegistryKey>> TCCRegistryConnection::ListKeys() {
  VarlinkObject *parameters = nullptr;
  if (varlink_object_new(&parameters) != 0)
    return std::nullopt;

  std::optional<std::vector<TCCRegistryKey>> keys;
  call(TCC_REGISTRY_INTERFACE ".ListKeys", parameters,
       [&](VarlinkObject *reply) {
         VarlinkArray *array = nullptr;
         if (varlink_object_get_array(reply, "keys", &array) != 0)
           return;

         keys.emplace();
         long n = varlink_array_get_n_elements(array);
         for (long i = 0; i < n; i++) {
           VarlinkObject *object = nullptr;
           const char *name = nullptr;
           const char *type = nullptr;
           const char *description = "";
           VarlinkObject *def = nullptr;
           if (varlink_array_get_object(array, i, &object) != 0 ||
               varlink_object_get_string(object, "name", &name) != 0 ||
               varlink_object_get_string(object, "type", &type) != 0 ||
               varlink_object_get_object(object, "default", &def) != 0)
             continue;
           varlink_object_get_string(object, "description", &description);

           auto parsed_type = tcc_registry_type_from_name(type);
           auto parsed_def = tcc_registry_value_from_varlink(def);
           if (!parsed_type || !parsed_def)
             continue;
           keys->push_back({name, *parsed_type, *parsed_def, description});
         }
       });
  return keys;
}

TCCRegistrySubscription::TCCRegistrySubscription(OnChange on_change)
    : mOnChange(std::move(on_change)) {
  subscribe();
}

TCCRegistrySubscription::~TCCRegistrySubscription() { disconnect(); }

void TCCRegistrySubscription::disconnect() {
  if (mConn)
    mConn = varlink_connection_free(mConn);
}

bool TCCRegistrySubscription::subscribe() {
  if (mConn)
    return true;

  std::string address = tcc_registry_address();
  if (address.empty())
    return false;
  // Not worth a message: tcc_registry not running yet is expected.
  if (varlink_connection_new(&mConn, address.c_str()) != 0) {
    mConn = nullptr;
    return false;
  }

  SigpipeBlock sigpipe_block;
  VarlinkObject *parameters = nullptr;
  varlink_object_new(&parameters);
  mEnded = false;
  long error = varlink_connection_call(mConn, TCC_REGISTRY_INTERFACE ".Subscribe",
                                       parameters, VARLINK_CALL_MORE, reply,
                                       this);
  varlink_object_unref(parameters);
  if (error != 0) {
    printf("[registry] varlink_connection_call error: %s\n",
           varlink_error_string(-error));
    disconnect();
    return false;
  }
  return true;
}

long TCCRegistrySubscription::reply(VarlinkConnection *connection,
                                    const char *error,
                                    VarlinkObject *parameters, uint64_t flags,
                                    void *userdata) {
  auto *self = static_cast<TCCRegistrySubscription *>(userdata);
  if (error) {
    printf("[registry] Subscribe: %s\n", error);
    self->mEnded = true;
    return 0;
  }
  if (!(flags & VARLINK_REPLY_CONTINUES))
    self->mEnded = true;

  const char *key = nullptr;
  VarlinkObject *object = nullptr;
  if (varlink_object_get_string(parameters, "key", &key) != 0 ||
      varlink_object_get_object(parameters, "value", &object) != 0)
    return 0;
  auto value = tcc_registry_value_from_varlink(object);
  if (value && self->mOnChange)
    self->mOnChange(key, *value);
  return 0;
}

int TCCRegistrySubscription::fd() {
  return mConn ? varlink_connection_get_fd(mConn) : -1;
}

short TCCRegistrySubscription::events() {
  return mConn ? varlink_connection_get_events(mConn) : 0;
}

void TCCRegistrySubscription::process(short revents) {
  if (!mConn || !revents)
    return;

  SigpipeBlock sigpipe_block;
  long error = varlink_connection_process_events(mConn, revents);
  if (error != 0 || mEnded || varlink_connection_is_closed(mConn)) {
    if (error != 0 && error != -VARLINK_ERROR_CONNECTION_CLOSED)
      printf("[registry] varlink_connection_process_events error: %s\n",
             varlink_error_string(-error));
    disconnect();
  }
}

static void __attribute__((constructor)) setup(void) {
  if (!dynload_setup::varlink()) {
    raise(SIGTRAP);
  }
}
