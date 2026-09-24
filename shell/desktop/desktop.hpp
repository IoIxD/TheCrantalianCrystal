#pragma once

#include "../client/client.hpp"
#include "../utils/glyph.hpp"

#include "../protocol/wlr-layer-shell-unstable-v1-protocol.h"
#include <memory>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>

#define ICON_SIZE 64
#define ICON_MARGIN ICON_SIZE + 32

/*
 * Client responsible for
 * a.) rendering the desktop
 * b.) rendering the icons of minimized windows
 */

class TCCDesktopClient {
  TCCClient::Output *mOutput;

  wl_display *mDisplay;
  wl_registry *mRegistry;
  wl_compositor *mCompositor;

  wl_surface *mSurface;

  wl_egl_window *mEGLWindow;
  EGLDisplay mEGLDisplay;
  EGLContext mEGLContext;
  EGLConfig mEGLConfig;
  EGLSurface mEGLSurface;

  struct IconInf {
    int width = 0;
    int height = 0;
    GLuint id = 0;
  };
  IconManager mIconManager;
  std::unordered_map<TCCClient::Window *, IconInf> mWindowIcons;

  zwlr_layer_shell_v1 *mLayerShell;
  zwlr_layer_surface_v1 *mLayerSurface;

  GLuint mTexture;

  GlyphManager mGlyphManager;

  void setup_egl();
  void draw_desktop();
  void draw_icon(TCCClient::Window *win, int x, int y);
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

  const zwlr_layer_surface_v1_listener mLayerSurfaceListener = {
      .configure = layer_surface_configure,
      .closed = layer_surface_closed,
  };

public:
  TCCDesktopClient(TCCClient::Output *output);
  void step();
  ~TCCDesktopClient();
};
