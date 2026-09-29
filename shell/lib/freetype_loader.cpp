#include "freetype_loader.hpp"

#include <cstdio>
#include <dlfcn.h>
#include <initializer_list>

bool FreetypeLib::load() {
  for (const char *soname : {"libfreetype.so.6", "libfreetype.so"}) {
    mHandle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (mHandle)
      break;
  }
  if (!mHandle) {
    fprintf(stderr, "tcc_shell: could not load freetype: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  name = reinterpret_cast<decltype(name)>(dlsym(mHandle, #name));              \
  if (!name) {                                                                 \
    fprintf(stderr, "tcc_shell: freetype is missing symbol %s\n", #name);      \
    dlclose(mHandle);                                                          \
    mHandle = nullptr;                                                         \
    return false;                                                              \
  }
  TCC_FREETYPE_FUNCS(X)
#undef X

  return true;
}

FreetypeLib *FreetypeLib::get() {
  static FreetypeLib lib;
  static bool loaded = lib.load();
  return loaded ? &lib : nullptr;
}
