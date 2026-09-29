#include "egl_loader.hpp"

#include <cstdio>
#include <dlfcn.h>
#include <initializer_list>

static void *open_library(std::initializer_list<const char *> sonames,
                          const char *what) {
  for (const char *soname : sonames) {
    void *handle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (handle)
      return handle;
  }
  fprintf(stderr, "tcc_shell: could not load %s: %s\n", what, dlerror());
  return nullptr;
}

void EGLLib::unload() {
  if (mHandle)
    dlclose(mHandle);
  if (mWaylandHandle)
    dlclose(mWaylandHandle);
  mHandle = nullptr;
  mWaylandHandle = nullptr;
}

bool EGLLib::load() {
  mHandle = open_library({"libEGL.so.1", "libEGL.so"}, "libEGL");
  mWaylandHandle = open_library({"libwayland-egl.so.1", "libwayland-egl.so"},
                                "libwayland-egl");
  if (!mHandle || !mWaylandHandle) {
    unload();
    return false;
  }

#define X(name, handle, what)                                                  \
  name = reinterpret_cast<decltype(name)>(dlsym(handle, #name));               \
  if (!name) {                                                                 \
    fprintf(stderr, "tcc_shell: %s is missing symbol %s\n", what, #name);      \
    unload();                                                                  \
    return false;                                                              \
  }
#define X_EGL(name) X(name, mHandle, "libEGL")
#define X_WAYLAND(name) X(name, mWaylandHandle, "libwayland-egl")
  TCC_EGL_FUNCS(X_EGL)
  TCC_WAYLAND_EGL_FUNCS(X_WAYLAND)
#undef X_WAYLAND
#undef X_EGL
#undef X

  return true;
}

EGLLib *EGLLib::get() {
  static EGLLib lib;
  static bool loaded = lib.load();
  return loaded ? &lib : nullptr;
}
