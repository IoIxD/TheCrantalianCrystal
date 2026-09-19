#pragma once

#include "../client/client.hpp"

#include "../protocol/wlr-layer-shell-unstable-v1-protocol.h"
#include <memory>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>

/*
 * Client responsible for
 * a.) rendering the desktop
 * b.) rendering the icons of open windows (TODO)
 */

class TCCDesktopClient {
  std::shared_ptr<TCCClient> mClient;

  wl_display *mDisplay;
  wl_registry *mRegistry;
  wl_compositor *mCompositor;
  wl_output *mOutput;

  wl_surface *mSurface;

  wl_egl_window *mEGLWindow;
  EGLDisplay mEGLDisplay;
  EGLContext mEGLContext;
  EGLConfig mEGLConfig;
  EGLSurface mEGLSurface;

  zwlr_layer_shell_v1 *mLayerShell;
  zwlr_layer_surface_v1 *mLayerSurface;

  GLuint mTexture;

  int mWidth = 1024;
  int mHeight = 768;

  void setup_egl();
  void egl_draw();

  static void registry_global(void *data, struct wl_registry *wl_registry,
                              uint32_t name, const char *interface,
                              uint32_t version);
  static void global_remove(void *data, struct wl_registry *wl_registry,
                            uint32_t name);

  static void layer_surface_configure(void *data,
                                      struct zwlr_layer_surface_v1 *surface,
                                      uint32_t serial, uint32_t w, uint32_t h);
  static void layer_surface_closed(void *data,
                                   struct zwlr_layer_surface_v1 *surface);

  const wl_registry_listener mRegistryListener = {
      .global = registry_global,
      .global_remove = global_remove,
  };

  static void output_geometry(void *data, struct wl_output *wl_output,
                              int32_t x, int32_t y, int32_t physical_width,
                              int32_t physical_height, int32_t subpixel,
                              const char *make, const char *model,
                              int32_t transform);
  static void output_mode(void *data, struct wl_output *wl_output,
                          uint32_t flags, int32_t width, int32_t height,
                          int32_t refresh);
  const wl_output_listener mOutputListener = {
      .geometry = output_geometry,
      .mode = output_mode,
  };

  const zwlr_layer_surface_v1_listener mLayerSurfaceListener = {
      .configure = layer_surface_configure,
      .closed = layer_surface_closed,
  };

public:
  TCCDesktopClient(std::shared_ptr<TCCClient> client);
  void run();
};
