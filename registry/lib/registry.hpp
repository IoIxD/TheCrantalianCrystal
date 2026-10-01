#pragma once

#include "registry_value.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

/*
 * A connection to tcc_registry, made on the first call (so it's fine to
 * create before tcc_registry is up). Calls block until it replies (or
 * CALL_TIMEOUT_MS), and reconnect first if it went away.
 */
class TCCRegistryConnection {
  VarlinkConnection *mConn = nullptr;

  // The call in flight; there's only ever one.
  bool mDone = false;
  std::string mError;
  std::function<void(VarlinkObject *)> mOnReply;

  static long reply(VarlinkConnection *connection, const char *error,
                    VarlinkObject *parameters, uint64_t flags,
                    void *userdata);

  enum class Result { Done, Lost, Failed };

  bool connect();
  void disconnect();
  // One try at calling `method`. Lost if the connection broke before a reply.
  Result attempt(const char *method, VarlinkObject *parameters);
  // Calls `method` with `parameters` (which it takes), passing the reply to
  // `on_reply`. False if there was no reply, or an error one.
  bool call(const char *method, VarlinkObject *parameters,
            std::function<void(VarlinkObject *)> on_reply = {});

public:
  TCCRegistryConnection() = default;
  ~TCCRegistryConnection();
  TCCRegistryConnection(const TCCRegistryConnection &) = delete;
  TCCRegistryConnection &operator=(const TCCRegistryConnection &) = delete;

  // Connects now, if not connected already. Calls do this themselves.
  bool open();
  // The varlink error name the last failed call got, if it got one.
  const std::string &lastError() const { return mError; }

  // The key's value (or its default), or nullopt if the key isn't valid or
  // tcc_registry can't be reached.
  std::optional<TCCRegistryValue> GetValue(const std::string &key);
  // Same, but also nullopt if the key isn't a T.
  template <typename T> std::optional<T> GetValue(const std::string &key) {
    auto value = GetValue(key);
    if (!value || !std::holds_alternative<T>(*value))
      return std::nullopt;
    return std::get<T>(*value);
  }

  // False if the key isn't valid, the value isn't of its type, or
  // tcc_registry can't be reached.
  bool SetValue(const std::string &key, const TCCRegistryValue &value);

  std::optional<std::vector<TCCRegistryKey>> ListKeys();
};

/*
 * Changes to tcc_registry's values, for an event loop: poll fd() for
 * events() and pass what comes back to process(), which calls `on_change` for
 * each change. Has a connection of its own, since the call stays open.
 *
 * If tcc_registry goes away, the subscription ends and fd() is -1 until
 * subscribe() succeeds again.
 */
class TCCRegistrySubscription {
public:
  using OnChange =
      std::function<void(const std::string &key, const TCCRegistryValue &)>;

private:
  VarlinkConnection *mConn = nullptr;
  OnChange mOnChange;
  // Set by reply() when the call is over, since the connection can't be
  // freed from inside its own callback.
  bool mEnded = false;

  static long reply(VarlinkConnection *connection, const char *error,
                    VarlinkObject *parameters, uint64_t flags,
                    void *userdata);

  void disconnect();

public:
  explicit TCCRegistrySubscription(OnChange on_change);
  ~TCCRegistrySubscription();
  TCCRegistrySubscription(const TCCRegistrySubscription &) = delete;
  TCCRegistrySubscription &operator=(const TCCRegistrySubscription &) = delete;

  // Subscribes, unless already subscribed. False if tcc_registry can't be
  // reached.
  bool subscribe();
  bool subscribed() const { return mConn != nullptr; }

  int fd();
  short events();
  void process(short revents);
};
