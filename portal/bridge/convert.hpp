#pragma once

#include "dbus_loader.hpp"
#include "varlink_loader.hpp"

#include <string>
#include <vector>

/*
 * Converts values between D-Bus and varlink, the same way in both directions:
 *
 *   b                      bool
 *   y n q i u x t          int
 *   d                      float
 *   s o g                  string
 *   h                      string: a /proc/<pid>/fd/<n> path for the fd
 *   v                      Variant (signature: string, value: <as converted>)
 *   a{kT}                  [string]T, non-string keys as their decimal value
 *   aT                     []T
 *   (T...)                 object, the members as fields "0", "1", ...
 *
 * The D-Bus signature always drives it, varlink types are too coarse.
 */

// A place a converted value goes into: a field of an object, or the end of an
// array.
struct TCCVarlinkOut {
  VarlinkObject *object = nullptr;
  const char *field = nullptr;
  VarlinkArray *array = nullptr;
};

// A value to convert: a field of an object, or an element of an array.
struct TCCVarlinkIn {
  VarlinkObject *object = nullptr;
  const char *field = nullptr;
  VarlinkArray *array = nullptr;
  unsigned long index = 0;
};

// File descriptors that have to stay open until the varlink call is done.
struct TCCFds {
  std::vector<int> fds;
  ~TCCFds();
};

namespace tcc_convert {
// Length of the first complete type in `signature`.
size_t complete_type_length(const char *signature);
// Splits a signature into its complete types.
std::vector<std::string> split(const char *signature);

// Converts the value at `iter`. `fds` gets any file descriptors in it.
bool from_dbus(DBusMessageIter *iter, TCCVarlinkOut out, TCCFds &fds);
// Appends `in`, converted to the complete type `signature`, to `iter`.
bool to_dbus(TCCVarlinkIn in, const std::string &signature,
             DBusMessageIter *iter);
} // namespace tcc_convert
