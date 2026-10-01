#pragma once

/*
 * What tcc_registry and its clients share: setting types and values, and how
 * they go over varlink (see net.ioi-xd.tcc.registry.varlink).
 */

#include "varlink_loader.hpp"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <variant>

#define TCC_REGISTRY_INTERFACE "net.ioi-xd.tcc.registry"

enum class TCCRegistryType { Bool, Int, Float, String };

// Alternatives are in the same order as TCCRegistryType.
using TCCRegistryValue = std::variant<bool, int64_t, double, std::string>;

struct TCCRegistryKey {
  std::string name;
  TCCRegistryType type;
  TCCRegistryValue def;
  std::string description;
};

inline TCCRegistryType tcc_registry_type_of(const TCCRegistryValue &value) {
  return static_cast<TCCRegistryType>(value.index());
}

// Also the field names of a Value.
inline const char *tcc_registry_type_name(TCCRegistryType type) {
  switch (type) {
  case TCCRegistryType::Bool:
    return "bool";
  case TCCRegistryType::Int:
    return "int";
  case TCCRegistryType::Float:
    return "float";
  case TCCRegistryType::String:
    return "string";
  }
  return "";
}

inline std::optional<TCCRegistryType>
tcc_registry_type_from_name(const std::string &name) {
  for (auto type : {TCCRegistryType::Bool, TCCRegistryType::Int,
                    TCCRegistryType::Float, TCCRegistryType::String}) {
    if (name == tcc_registry_type_name(type))
      return type;
  }
  return std::nullopt;
}

// Where tcc_registry listens, or "" without XDG_RUNTIME_DIR.
inline std::string tcc_registry_address() {
  const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (!runtime_dir || !*runtime_dir)
    return "";
  return std::string("unix:") + runtime_dir + "/tcc-registry.varlink";
}

// A new Value object for `value`, or nullptr.
inline VarlinkObject *tcc_registry_value_to_varlink(
    const TCCRegistryValue &value) {
  VarlinkObject *object = nullptr;
  if (varlink_object_new(&object) != 0)
    return nullptr;

  const char *field = tcc_registry_type_name(tcc_registry_type_of(value));
  long error = std::visit(
      [&](auto &&v) -> long {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>)
          return varlink_object_set_bool(object, field, v);
        else if constexpr (std::is_same_v<T, int64_t>)
          return varlink_object_set_int(object, field, v);
        else if constexpr (std::is_same_v<T, double>)
          return varlink_object_set_float(object, field, v);
        else
          return varlink_object_set_string(object, field, v.c_str());
      },
      value);
  if (error != 0) {
    varlink_object_unref(object);
    return nullptr;
  }
  return object;
}

// The value in a Value object, if exactly one of its fields is set.
inline std::optional<TCCRegistryValue>
tcc_registry_value_from_varlink(VarlinkObject *object) {
  std::optional<TCCRegistryValue> value;
  int set = 0;

  bool b;
  if (varlink_object_get_bool(object, "bool", &b) == 0) {
    value = b;
    set++;
  }
  int64_t i;
  if (varlink_object_get_int(object, "int", &i) == 0) {
    value = i;
    set++;
  }
  double f;
  if (varlink_object_get_float(object, "float", &f) == 0) {
    value = f;
    set++;
  }
  const char *s;
  if (varlink_object_get_string(object, "string", &s) == 0) {
    value = std::string(s);
    set++;
  }

  if (set != 1)
    return std::nullopt;
  return value;
}
