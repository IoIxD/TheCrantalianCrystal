#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-egl.h>

#define TCC_EGL_FUNCS(X)                                                       \
  X(eglBindAPI)                                                                \
  X(eglChooseConfig)                                                           \
  X(eglCreateContext)                                                          \
  X(eglCreatePlatformWindowSurface)                                            \
  X(eglDestroyContext)                                                         \
  X(eglDestroySurface)                                                         \
  X(eglGetConfigs)                                                             \
  X(eglGetError)                                                               \
  X(eglGetPlatformDisplay)                                                     \
  X(eglInitialize)                                                             \
  X(eglMakeCurrent)                                                            \
  X(eglSwapBuffers)                                                            \
  X(eglSwapInterval)

#define TCC_WAYLAND_EGL_FUNCS(X)                                               \
  X(wl_egl_window_create)                                                      \
  X(wl_egl_window_destroy)                                                     \
  X(wl_egl_window_resize)

struct EGLLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_EGL_FUNCS(X)
  TCC_WAYLAND_EGL_FUNCS(X)
#undef X

  // Returns the process-wide instance, or nullptr if libEGL or libwayland-egl
  // could not be loaded.
  static EGLLib *get();

private:
  void *mHandle = nullptr;
  void *mWaylandHandle = nullptr;
  bool load();
  void unload();
};
