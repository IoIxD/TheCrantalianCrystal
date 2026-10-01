#pragma once

#include "registry_value.hpp"
#include "sqlite_loader.hpp"

#include <optional>
#include <string>

/*
 * The sqlite database values are kept in: one row per key that's been set,
 * in a settings (key, value) table. Knows nothing about which keys are valid.
 */
class TCCRegistryStore {
  sqlite3 *mDB = nullptr;
  sqlite3_stmt *mGet = nullptr;
  sqlite3_stmt *mSet = nullptr;

public:
  TCCRegistryStore() = default;
  ~TCCRegistryStore();
  TCCRegistryStore(const TCCRegistryStore &) = delete;
  TCCRegistryStore &operator=(const TCCRegistryStore &) = delete;

  // Opens (or creates) the database at `path`.
  bool open(const std::string &path);

  // The value stored for `key`, if there is one of `type`.
  std::optional<TCCRegistryValue> get(const std::string &key,
                                      TCCRegistryType type);
  bool set(const std::string &key, const TCCRegistryValue &value);
};
