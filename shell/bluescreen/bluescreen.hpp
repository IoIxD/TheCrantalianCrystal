#pragma once

#include "../client/client.hpp"
#include "../utils/glyph.hpp"

#include "../protocol/wlr-layer-shell-unstable-v1-protocol.h"
#include <chrono>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>

class TCCBluescreenClient {
  TCCClient::Output *mOutput;
  std::vector<std::string> mStacktrace;
  std::vector<std::pair<std::string, std::string>> mRegisters;

  std::chrono::system_clock::time_point mClock =
      std::chrono::system_clock::now();

  wl_display *mDisplay;
  wl_registry *mRegistry;
  wl_compositor *mCompositor;

  wl_surface *mSurface;

  wl_egl_window *mEGLWindow;
  EGLDisplay mEGLDisplay;
  EGLContext mEGLContext;
  EGLConfig mEGLConfig;
  EGLSurface mEGLSurface;

  zwlr_layer_shell_v1 *mLayerShell;
  zwlr_layer_surface_v1 *mLayerSurface;

  GlyphManager mGlyphManager;
  GlyphManager mGlyphManagerTwo;

  void setup_egl();
  void draw_text();
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
  TCCBluescreenClient(
      TCCClient::Output *output, std::vector<std::string> stacktrace,
      std::vector<std::pair<std::string, std::string>> registers);
  void run();
  wl_display *display() { return mDisplay; };
  ~TCCBluescreenClient();
  uint64_t seconds() {
    auto now = std::chrono::system_clock::now();
    return (now - mClock).count() / 1000000000;
  }
};
