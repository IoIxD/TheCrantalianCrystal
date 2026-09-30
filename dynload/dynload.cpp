#include "dynload.hpp"
#include <dlfcn.h>
#include <initializer_list>

#define TCC_DYNLOAD_SKIP_DEFINES
#include "dbus_loader.hpp"
#include "egl_loader.hpp"
#include "freetype_loader.hpp"
#include "gl_loader.hpp"
#include "gtk_loader.hpp"
#include "pulse_loader.hpp"
#include "varlink_loader.hpp"
#include "wayland_loader.h"

WaylandLib *WL_LIB = nullptr;
PulseLib *PL_LIB = nullptr;
GtkLib *GTK_LIB = nullptr;
GLLib *GL_LIB = nullptr;
FreetypeLib *FT_LIB = nullptr;
EGLLib *EGL_LIB = nullptr;
DBusLib *DBUS_LIB = nullptr;
VarlinkLib *VARLINK_LIB = nullptr;

static void *open_library(std::initializer_list<const char *> sonames,
                          const char *what = "") {
  for (const char *soname : sonames) {
    void *handle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (handle)
      return handle;
  }
  fprintf(stderr, "tcc_shell: could not load %s: %s\n", what, dlerror());
  return nullptr;
}

namespace dynload_setup {
bool wayland() {
  WL_LIB = new WaylandLib();

  WL_LIB->handle =
      open_library({"libwayland-client.so.0", "libwayland-client.so"});

  if (!WL_LIB->handle) {
    fprintf(stderr, "could not load libwayland-client: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  WL_LIB->name =                                                               \
      reinterpret_cast<decltype(WL_LIB->name)>(dlsym(WL_LIB->handle, #name));  \
  if (!WL_LIB->name) {                                                         \
    fprintf(stderr, "libwayland-client is missing symbol %s\n", #name);        \
    dlclose(WL_LIB->handle);                                                   \
    WL_LIB->handle = nullptr;                                                  \
    return false;                                                              \
  }
  TCC_WAYLAND_FUNCS(X)
#undef X

  return true;
}

bool pulse() {
  PL_LIB = new PulseLib();

  PL_LIB->handle = open_library({"libpulse.so.0", "libpulse.so"});

  if (!PL_LIB->handle) {
    fprintf(stderr, "could not load libpulse: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  PL_LIB->name =                                                               \
      reinterpret_cast<decltype(PL_LIB->name)>(dlsym(PL_LIB->handle, #name));  \
  if (!PL_LIB->name) {                                                         \
    fprintf(stderr, "libpulse is missing symbol %s\n", #name);                 \
    dlclose(PL_LIB->handle);                                                   \
    PL_LIB->handle = nullptr;                                                  \
    return false;                                                              \
  }
  TCC_PULSE_FUNCS(X)
#undef X

  return true;
}

bool gtk() {
  GTK_LIB = new GtkLib();
  GTK_LIB->handle = open_library({"libgtk-4.so.1", "libgtk-4.so"});

  if (!GTK_LIB->handle) {
    fprintf(stderr, "tcc_iconlib: could not load gtk4: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  GTK_LIB->name = reinterpret_cast<decltype(GTK_LIB->name)>(                   \
      dlsym(GTK_LIB->handle, #name));                                          \
  if (!GTK_LIB->name) {                                                        \
    fprintf(stderr, "tcc_iconlib: gtk4 is missing symbol %s\n", #name);        \
    dlclose(GTK_LIB->handle);                                                  \
    GTK_LIB->handle = nullptr;                                                 \
    return false;                                                              \
  }
  TCC_GTK_FUNCS(X)
#undef X

  return true;
}

bool gl() {
  GL_LIB = new GLLib();
  GL_LIB->handle = open_library({"libGL.so.1", "libGL.so"});

  if (!GL_LIB->handle) {
    fprintf(stderr, "could not load libGL: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  GL_LIB->name =                                                               \
      reinterpret_cast<decltype(GL_LIB->name)>(dlsym(GL_LIB->handle, #name));  \
  if (!GL_LIB->name) {                                                         \
    fprintf(stderr, "libGL is missing symbol %s\n", #name);                    \
    dlclose(GL_LIB->handle);                                                   \
    GL_LIB->handle = nullptr;                                                  \
    return false;                                                              \
  }
  TCC_GL_FUNCS(X)
#undef X

  return true;
}

bool freetype() {
  FT_LIB = new FreetypeLib();
  FT_LIB->handle = open_library({"libfreetype.so.6", "libfreetype.so"});

  if (!FT_LIB->handle) {
    fprintf(stderr, "tcc_shell: could not load freetype: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  FT_LIB->name =                                                               \
      reinterpret_cast<decltype(FT_LIB->name)>(dlsym(FT_LIB->handle, #name));  \
  if (!FT_LIB->name) {                                                         \
    fprintf(stderr, "tcc_shell: freetype is missing symbol %s\n", #name);      \
    dlclose(FT_LIB->handle);                                                   \
    FT_LIB->handle = nullptr;                                                  \
    return false;                                                              \
  }
  TCC_FREETYPE_FUNCS(X)
#undef X

  return true;
}

bool egl() {
  EGL_LIB = new EGLLib();

  EGL_LIB->handle = open_library({"libEGL.so.1", "libEGL.so"}, "libEGL");
  EGL_LIB->wl_handle = open_library(
      {"libwayland-egl.so.1", "libwayland-egl.so"}, "libwayland-egl");
  if (!EGL_LIB->handle || !EGL_LIB->wl_handle) {
    return false;
  }

#define X(name, handle, what)                                                  \
  EGL_LIB->name =                                                              \
      reinterpret_cast<decltype(EGL_LIB->name)>(dlsym(handle, #name));         \
  if (!EGL_LIB->name) {                                                        \
    fprintf(stderr, "tcc_shell: %s is missing symbol %s\n", what, #name);      \
    return false;                                                              \
  }
#define X_EGL(name) X(name, EGL_LIB->handle, "libEGL")
#define X_WAYLAND(name) X(name, EGL_LIB->wl_handle, "libwayland-egl")
  TCC_EGL_FUNCS(X_EGL)
  TCC_WAYLAND_EGL_FUNCS(X_WAYLAND)
#undef X_WAYLAND
#undef X_EGL
#undef X

  return true;
}

bool dbus() {
  DBUS_LIB = new DBusLib();

  for (const char *soname : {"libdbus-1.so.3", "libdbus-1.so"}) {
    DBUS_LIB->handle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (DBUS_LIB->handle)
      break;
  }
  if (!DBUS_LIB->handle) {
    fprintf(stderr, "could not load libdbus-1: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  DBUS_LIB->name = reinterpret_cast<decltype(DBUS_LIB->name)>(                 \
      dlsym(DBUS_LIB->handle, #name));                                         \
  if (!DBUS_LIB->name) {                                                       \
    fprintf(stderr, "libdbus-1 is missing symbol %s\n", #name);                \
    dlclose(DBUS_LIB->handle);                                                 \
    DBUS_LIB->handle = nullptr;                                                \
    return false;                                                              \
  }
  TCC_DBUS_FUNCS(X)
#undef X

  return true;
}

bool varlink() {
  VARLINK_LIB = new VarlinkLib();

  for (const char *soname : {"libvarlink.so.0", "libvarlink.so"}) {
    VARLINK_LIB->handle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (VARLINK_LIB->handle)
      break;
  }
  if (!VARLINK_LIB->handle) {
    fprintf(stderr, "could not load libvarlink: %s\n", dlerror());
    return false;
  }

#define X(name)                                                                \
  VARLINK_LIB->name = reinterpret_cast<decltype(VARLINK_LIB->name)>(           \
      dlsym(VARLINK_LIB->handle, #name));                                      \
  if (!VARLINK_LIB->name) {                                                    \
    fprintf(stderr, "libvarlink is missing symbol %s\n", #name);               \
    dlclose(VARLINK_LIB->handle);                                              \
    VARLINK_LIB->handle = nullptr;                                             \
    return false;                                                              \
  }
  TCC_VARLINK_FUNCS(X)
#undef X

  return true;
}
} // namespace dynload_setup
