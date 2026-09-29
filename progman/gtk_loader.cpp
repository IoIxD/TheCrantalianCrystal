#include "gtk_loader.hpp"

#include <cstdio>
#include <dlfcn.h>
#include <initializer_list>

bool GtkLib::load() {
  for (const char *soname : {"libgtk-4.so.1", "libgtk-4.so"}) {
    mHandle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (mHandle)
      break;
  }
  if (!mHandle) {
    fprintf(stderr, "tcc_progman: could not load gtk4: %s\n", dlerror());
    return false;
  }

  // dlsym on a handle also searches the libraries it depends on, so the
  // glib and gio functions are found through gtk's handle.
#define X(name)                                                                \
  name = reinterpret_cast<decltype(name)>(dlsym(mHandle, #name));              \
  if (!name) {                                                                 \
    fprintf(stderr, "tcc_progman: gtk4 is missing symbol %s\n", #name);        \
    dlclose(mHandle);                                                          \
    mHandle = nullptr;                                                         \
    return false;                                                              \
  }
  TCC_GTK_FUNCS(X)
#undef X

  return true;
}

GtkLib *GtkLib::get() {
  static GtkLib lib;
  static bool loaded = lib.load();
  return loaded ? &lib : nullptr;
}
