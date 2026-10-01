#pragma once

#include "registry_value.hpp"

#include <string>
#include <vector>

// Every key the registry accepts.
const std::vector<TCCRegistryKey> &tcc_registry_keys();

// The key named `name`, or nullptr if it isn't valid.
const TCCRegistryKey *tcc_registry_find_key(const std::string &name);
