#include "keys.hpp"

/*
 * The valid keys. Values stored for keys that aren't here (anymore), or that
 * changed type, are ignored in favor of the default.
 */
static const std::vector<TCCRegistryKey> KEYS = {
    {"Dark Theme", TCCRegistryType::Bool, bool(true), "Preferred dark theme"},
    {"Idle Timeout", TCCRegistryType::Int, int64_t(5 * 60 * 1000),
     "Timeout in ms before the session locks"},
};

const std::vector<TCCRegistryKey> &tcc_registry_keys() { return KEYS; }

const TCCRegistryKey *tcc_registry_find_key(const std::string &name) {
  for (const TCCRegistryKey &key : KEYS) {
    if (key.name == name)
      return &key;
  }
  return nullptr;
}
