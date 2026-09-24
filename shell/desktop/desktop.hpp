#pragma once

#include "../client/client.hpp"
#include "../utils/glyph.hpp"

#include "../protocol/wlr-layer-shell-unstable-v1-protocol.h"
#include <functional>
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

  wl_seat *mSeat = nullptr;
  wl_pointer *mPointer = nullptr;

  wl_egl_window *mEGLWindow;
  EGLDisplay mEGLDisplay;
  EGLContext mEGLContext;
  EGLConfig mEGLConfig;
  EGLSurface mEGLSurface;

  double mPointerX = 0.0;
  double mPointerY = 0.0;

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

  void for_each_minimized(
      std::function<void(TCCClient::Window *, int x, int y)> func);

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

  static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat,
                                   uint32_t capabilities);
  static void wl_seat_name(void *data, struct wl_seat *wl_seat,
                           const char *name);
  const wl_seat_listener mWlSeatListener = {
      .capabilities = wl_seat_capabilities,
      .name = wl_seat_name,
  };

  static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                               uint32_t serial, struct wl_surface *surface,
                               wl_fixed_t surface_x, wl_fixed_t surface_y);
  static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                               uint32_t serial, struct wl_surface *surface);
  static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                                uint32_t time, wl_fixed_t surface_x,
                                wl_fixed_t surface_y);
  static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                                uint32_t serial, uint32_t time, uint32_t button,
                                uint32_t state);
  static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
                              uint32_t time, uint32_t axis, wl_fixed_t value);
  const wl_pointer_listener mWlPointerListener = {
      .enter = wl_pointer_enter,
      .leave = wl_pointer_leave,
      .motion = wl_pointer_motion,
      .button = wl_pointer_button,
      .axis = wl_pointer_axis,
  };

public:
  TCCDesktopClient(TCCClient::Output *output);
  void step();
  wl_display *display() { return mDisplay; };
  ~TCCDesktopClient();
};
