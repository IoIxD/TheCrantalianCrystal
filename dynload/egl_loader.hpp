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
  X(eglGetProcAddress)                                                         \
  X(eglInitialize)                                                             \
  X(eglMakeCurrent)                                                            \
  X(eglSwapBuffers)                                                            \
  X(eglSwapInterval)                                                           \
  X(eglTerminate)

#define TCC_WAYLAND_EGL_FUNCS(X)                                               \
  X(wl_egl_window_create)                                                      \
  X(wl_egl_window_destroy)                                                     \
  X(wl_egl_window_resize)

struct EGLLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_EGL_FUNCS(X)
  TCC_WAYLAND_EGL_FUNCS(X)
#undef X

  void *handle = nullptr;
  void *wl_handle = nullptr;
};

extern EGLLib *EGL_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define eglBindAPI EGL_LIB->eglBindAPI
#define eglChooseConfig EGL_LIB->eglChooseConfig
#define eglCreateContext EGL_LIB->eglCreateContext
#define eglCreatePlatformWindowSurface EGL_LIB->eglCreatePlatformWindowSurface
#define eglDestroyContext EGL_LIB->eglDestroyContext
#define eglDestroySurface EGL_LIB->eglDestroySurface
#define eglGetConfigs EGL_LIB->eglGetConfigs
#define eglGetError EGL_LIB->eglGetError
#define eglGetPlatformDisplay EGL_LIB->eglGetPlatformDisplay
#define eglGetProcAddress EGL_LIB->eglGetProcAddress
#define eglInitialize EGL_LIB->eglInitialize
#define eglMakeCurrent EGL_LIB->eglMakeCurrent
#define eglSwapBuffers EGL_LIB->eglSwapBuffers
#define eglSwapInterval EGL_LIB->eglSwapInterval
#define eglTerminate EGL_LIB->eglTerminate
#define wl_egl_window_create EGL_LIB->wl_egl_window_create
#define wl_egl_window_destroy EGL_LIB->wl_egl_window_destroy
#define wl_egl_window_resize EGL_LIB->wl_egl_window_resize
#endif
