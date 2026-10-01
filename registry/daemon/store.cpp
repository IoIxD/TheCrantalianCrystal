#include "store.hpp"

#include <cstdio>

TCCRegistryStore::~TCCRegistryStore() {
  sqlite3_finalize(mGet);
  sqlite3_finalize(mSet);
  if (mDB)
    sqlite3_close(mDB);
}

bool TCCRegistryStore::open(const std::string &path) {
  if (sqlite3_open_v2(path.c_str(), &mDB,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr) != SQLITE_OK) {
    fprintf(stderr, "tcc_registry: could not open %s: %s\n", path.c_str(),
            mDB ? sqlite3_errmsg(mDB) : "out of memory");
    return false;
  }
  sqlite3_busy_timeout(mDB, 1000);

  char *error = nullptr;
  if (sqlite3_exec(mDB,
                   "CREATE TABLE IF NOT EXISTS settings ("
                   "  key TEXT PRIMARY KEY NOT NULL,"
                   "  value NOT NULL"
                   ")",
                   nullptr, nullptr, &error) != SQLITE_OK) {
    fprintf(stderr, "tcc_registry: could not create the settings table: %s\n",
            error);
    sqlite3_free(error);
    return false;
  }

  if (sqlite3_prepare_v2(mDB, "SELECT value FROM settings WHERE key = ?1", -1,
                         &mGet, nullptr) != SQLITE_OK ||
      sqlite3_prepare_v2(
          mDB, "INSERT OR REPLACE INTO settings (key, value) VALUES (?1, ?2)",
          -1, &mSet, nullptr) != SQLITE_OK) {
    fprintf(stderr, "tcc_registry: could not prepare statements: %s\n",
            sqlite3_errmsg(mDB));
    return false;
  }
  return true;
}

std::optional<TCCRegistryValue> TCCRegistryStore::get(const std::string &key,
                                                      TCCRegistryType type) {
  sqlite3_reset(mGet);
  sqlite3_bind_text(mGet, 1, key.c_str(), -1, SQLITE_TRANSIENT);

  int ret = sqlite3_step(mGet);
  if (ret != SQLITE_ROW) {
    if (ret != SQLITE_DONE)
      fprintf(stderr, "tcc_registry: could not read %s: %s\n", key.c_str(),
              sqlite3_errmsg(mDB));
    return std::nullopt;
  }

  int column = sqlite3_column_type(mGet, 0);
  std::optional<TCCRegistryValue> value;
  switch (type) {
  case TCCRegistryType::Bool:
    if (column == SQLITE_INTEGER)
      value = sqlite3_column_int64(mGet, 0) != 0;
    break;
  case TCCRegistryType::Int:
    if (column == SQLITE_INTEGER)
      value = int64_t(sqlite3_column_int64(mGet, 0));
    break;
  case TCCRegistryType::Float:
    if (column == SQLITE_FLOAT || column == SQLITE_INTEGER)
      value = sqlite3_column_double(mGet, 0);
    break;
  case TCCRegistryType::String:
    if (column == SQLITE_TEXT)
      value = std::string(
          reinterpret_cast<const char *>(sqlite3_column_text(mGet, 0)));
    break;
  }
  sqlite3_reset(mGet);
  return value;
}

bool TCCRegistryStore::set(const std::string &key,
                           const TCCRegistryValue &value) {
  sqlite3_reset(mSet);
  sqlite3_bind_text(mSet, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  std::visit(
      [&](auto &&v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int64_t>)
          sqlite3_bind_int64(mSet, 2, v);
        else if constexpr (std::is_same_v<T, double>)
          sqlite3_bind_double(mSet, 2, v);
        else
          sqlite3_bind_text(mSet, 2, v.c_str(), -1, SQLITE_TRANSIENT);
      },
      value);

  int ret = sqlite3_step(mSet);
  sqlite3_reset(mSet);
  if (ret != SQLITE_DONE) {
    fprintf(stderr, "tcc_registry: could not write %s: %s\n", key.c_str(),
            sqlite3_errmsg(mDB));
    return false;
  }
  return true;
}
