#ifdef TCC_SYSTRAY_PULSE
#include "pulse_loader.hpp"

#include <cstdio>
#include <dlfcn.h>
#include <initializer_list>

bool PulseLib::load() {
  for (const char *soname : {"libpulse.so.0", "libpulse.so"}) {
    mHandle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (mHandle)
      break;
  }
  if (!mHandle) {
    fprintf(stderr, "tcc_systray: could not load libpulse: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  name = reinterpret_cast<decltype(name)>(dlsym(mHandle, #name));              \
  if (!name) {                                                                 \
    fprintf(stderr, "tcc_systray: libpulse is missing symbol %s\n", #name);    \
    dlclose(mHandle);                                                          \
    mHandle = nullptr;                                                         \
    return false;                                                              \
  }
  TCC_PULSE_FUNCS(X)
#undef X

  return true;
}

PulseLib *PulseLib::get() {
  static PulseLib lib;
  static bool loaded = lib.load();
  return loaded ? &lib : nullptr;
}
#endif
