#ifdef TCC_SYSTRAY_DBUS
#include "dbus_loader.hpp"

#include <cstdio>
#include <dlfcn.h>
#include <initializer_list>

bool DBusLib::load() {
  for (const char *soname : {"libdbus-1.so.3", "libdbus-1.so"}) {
    mHandle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (mHandle)
      break;
  }
  if (!mHandle) {
    fprintf(stderr, "tcc_systray: could not load libdbus-1: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  name = reinterpret_cast<decltype(name)>(dlsym(mHandle, #name));              \
  if (!name) {                                                                 \
    fprintf(stderr, "tcc_systray: libdbus-1 is missing symbol %s\n", #name);   \
    dlclose(mHandle);                                                          \
    mHandle = nullptr;                                                         \
    return false;                                                              \
  }
  TCC_DBUS_FUNCS(X)
#undef X

  return true;
}

DBusLib *DBusLib::get() {
  static DBusLib lib;
  static bool loaded = lib.load();
  return loaded ? &lib : nullptr;
}
#endif
