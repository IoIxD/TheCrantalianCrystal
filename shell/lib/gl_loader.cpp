#include "gl_loader.hpp"

#include <cstdio>
#include <dlfcn.h>
#include <initializer_list>

bool GLLib::load() {
  for (const char *soname : {"libGL.so.1", "libGL.so"}) {
    mHandle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (mHandle)
      break;
  }
  if (!mHandle) {
    fprintf(stderr, "tcc_shell: could not load libGL: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  name = reinterpret_cast<decltype(name)>(dlsym(mHandle, #name));              \
  if (!name) {                                                                 \
    fprintf(stderr, "tcc_shell: libGL is missing symbol %s\n", #name);         \
    dlclose(mHandle);                                                          \
    mHandle = nullptr;                                                         \
    return false;                                                              \
  }
  TCC_GL_FUNCS(X)
#undef X

  return true;
}

GLLib *GLLib::get() {
  static GLLib lib;
  static bool loaded = lib.load();
  return loaded ? &lib : nullptr;
}
