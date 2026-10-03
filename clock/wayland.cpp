#include "clock.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <poll.h>

using namespace clock_config;

void ClockWindow::registry_global(void *data, wl_registry *registry,
                                  uint32_t name, const char *interface,
                                  uint32_t version) {
  ClockWindow *clock = (ClockWindow *)data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    clock->mCompositor = (wl_compositor *)wl_registry_bind(
        registry, name, &wl_compositor_interface, std::min(version, 4u));
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    clock->mLayerShell = (zwlr_layer_shell_v1 *)wl_registry_bind(
        registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u));
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    // The same output the backdrop was captured from.
    if (!clock->mOutput &&
        (clock->mOutputName == 0 || clock->mOutputName == name)) {
      clock->mOutput = (wl_output *)wl_registry_bind(
          registry, name, &wl_output_interface, std::min(version, 2u));
      wl_output_add_listener(clock->mOutput, &clock->mOutputListener, clock);
    }
  }
}

void ClockWindow::registry_global_remove(void *, wl_registry *, uint32_t) {}

void ClockWindow::output_geometry(void *, wl_output *, int32_t, int32_t,
                                  int32_t, int32_t, int32_t, const char *,
                                  const char *, int32_t) {}

void ClockWindow::output_mode(void *, wl_output *, uint32_t, int32_t, int32_t,
                              int32_t) {}

void ClockWindow::output_done(void *, wl_output *) {}

void ClockWindow::output_scale(void *data, wl_output *, int32_t factor) {
  ((ClockWindow *)data)->mScale = std::max(factor, 1);
}

void ClockWindow::layer_surface_configure(void *data,
                                          zwlr_layer_surface_v1 *layer_surface,
                                          uint32_t serial, uint32_t, uint32_t) {
  // (it's always the size it asked for)
  zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
  ((ClockWindow *)data)->mConfigured = true;
}

void ClockWindow::layer_surface_closed(void *data, zwlr_layer_surface_v1 *) {
  ((ClockWindow *)data)->mClosed = true;
}

// Puts a layer surface over everything at the clock's spot on its output.
bool ClockWindow::setup_surface() {
  mRegistry = wl_display_get_registry(mDisplay);
  wl_registry_add_listener(mRegistry, &mRegistryListener, this);
  // (once for the globals, and again for the output's scale)
  wl_display_roundtrip(mDisplay);
  wl_display_roundtrip(mDisplay);
  if (!mCompositor || !mLayerShell) {
    fprintf(stderr,
            "tcc_clock: wl_compositor or zwlr_layer_shell_v1 not supported\n");
    return false;
  }

  mSurface = wl_compositor_create_surface(mCompositor);
  mLayerSurface = zwlr_layer_shell_v1_get_layer_surface(
      mLayerShell, mSurface, mOutput, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
      "tcc-clock");
  zwlr_layer_surface_v1_set_anchor(mLayerSurface,
                                   ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
  zwlr_layer_surface_v1_set_margin(mLayerSurface, WINDOW_Y, 0, 0, WINDOW_X);
  zwlr_layer_surface_v1_set_size(mLayerSurface, WINDOW_WIDTH, WINDOW_HEIGHT);
  // (measured from the output's edge, not inside any panels, so it's over
  // what was captured)
  zwlr_layer_surface_v1_set_exclusive_zone(mLayerSurface, -1);
  zwlr_layer_surface_v1_add_listener(mLayerSurface, &mLayerSurfaceListener,
                                     this);

  // It's only to look at; clicks go through to whatever's under it.
  wl_region *region = wl_compositor_create_region(mCompositor);
  wl_surface_set_input_region(mSurface, region);
  wl_region_destroy(region);

  wl_surface_set_buffer_scale(mSurface, mScale);
  wl_surface_commit(mSurface);

  while (!mConfigured && !mClosed) {
    if (wl_display_roundtrip(mDisplay) < 0) {
      return false;
    }
  }
  return !mClosed;
}

bool ClockWindow::setup_egl() {
  // (with alpha, for the rounded corners)
  EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                             EGL_WINDOW_BIT,
                             EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_BIT,
                             EGL_RED_SIZE,
                             8,
                             EGL_GREEN_SIZE,
                             8,
                             EGL_BLUE_SIZE,
                             8,
                             EGL_ALPHA_SIZE,
                             8,
                             EGL_NONE};
  EGLint major, minor, n;
  EGLConfig config;

  mEGLDisplay = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, mDisplay, NULL);
  if (mEGLDisplay == EGL_NO_DISPLAY ||
      !eglInitialize(mEGLDisplay, &major, &minor) ||
      !eglChooseConfig(mEGLDisplay, config_attribs, &config, 1, &n) || n < 1 ||
      !eglBindAPI(EGL_OPENGL_API)) {
    fprintf(stderr, "tcc_clock: couldn't set up EGL: %04X\n", eglGetError());
    return false;
  }

  mEGLWindow = wl_egl_window_create(mSurface, WINDOW_WIDTH * mScale,
                                    WINDOW_HEIGHT * mScale);
  mEGLContext = eglCreateContext(mEGLDisplay, config, EGL_NO_CONTEXT, NULL);
  if (!mEGLWindow || mEGLContext == EGL_NO_CONTEXT) {
    fprintf(stderr, "tcc_clock: couldn't create a GL context: %04X\n",
            eglGetError());
    return false;
  }
  mEGLSurface =
      eglCreatePlatformWindowSurface(mEGLDisplay, config, mEGLWindow, NULL);
  if (mEGLSurface == EGL_NO_SURFACE ||
      !eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext)) {
    fprintf(stderr, "tcc_clock: couldn't create an EGL surface: %04X\n",
            eglGetError());
    return false;
  }
  // The main loop paces itself, and swapping mustn't wait on a frame callback
  // that won't come while the clock's hidden (under the lock screen, say), or
  // it couldn't fade out and quit.
  eglSwapInterval(mEGLDisplay, 0);
  return true;
}

bool ClockWindow::dispatch(int timeout_ms) {
  while (wl_display_prepare_read(mDisplay) != 0) {
    if (wl_display_dispatch_pending(mDisplay) < 0) {
      return false;
    }
  }
  wl_display_flush(mDisplay);

  pollfd pfd = {wl_display_get_fd(mDisplay), POLLIN, 0};
  int ready = poll(&pfd, 1, timeout_ms);
  if (ready <= 0) {
    wl_display_cancel_read(mDisplay);
    // (a signal coming in isn't a problem)
    return ready == 0 || errno == EINTR;
  }
  if (wl_display_read_events(mDisplay) < 0) {
    return false;
  }
  return wl_display_dispatch_pending(mDisplay) >= 0;
}

void ClockWindow::swap() { eglSwapBuffers(mEGLDisplay, mEGLSurface); }

void ClockWindow::teardown_surface() {
  if (mEGLDisplay != EGL_NO_DISPLAY) {
    eglMakeCurrent(mEGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (mEGLSurface != EGL_NO_SURFACE)
      eglDestroySurface(mEGLDisplay, mEGLSurface);
    if (mEGLContext != EGL_NO_CONTEXT)
      eglDestroyContext(mEGLDisplay, mEGLContext);
  }
  if (mEGLWindow)
    wl_egl_window_destroy(mEGLWindow);
  if (mEGLDisplay != EGL_NO_DISPLAY)
    eglTerminate(mEGLDisplay);
  if (mLayerSurface)
    zwlr_layer_surface_v1_destroy(mLayerSurface);
  if (mSurface)
    wl_surface_destroy(mSurface);
  if (mLayerShell)
    zwlr_layer_shell_v1_destroy(mLayerShell);
  if (mOutput)
    wl_proxy_destroy((wl_proxy *)mOutput);
  if (mCompositor)
    wl_compositor_destroy(mCompositor);
  if (mRegistry)
    wl_registry_destroy(mRegistry);
  if (mDisplay)
    wl_display_disconnect(mDisplay);
}
