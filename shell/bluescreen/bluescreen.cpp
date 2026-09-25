#include "bluescreen.hpp"
#include <GL/gl.h>
#include <assert.h>
#include <csignal>

#include <EGL/eglext.h>

void TCCBluescreenClient::layer_surface_configure(
    void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t w, uint32_t h) {
  TCCBluescreenClient *client = (TCCBluescreenClient *)data;
  /* ignore the width and height set we fucken damn well know what we're setting
   * it to.) */
  zwlr_layer_surface_v1_ack_configure(surface, serial);
}
void TCCBluescreenClient::layer_surface_closed(
    void *data, struct zwlr_layer_surface_v1 *surface) {
  TCCBluescreenClient *client = (TCCBluescreenClient *)data;
  eglDestroySurface(client->mEGLDisplay, client->mEGLSurface);
  wl_egl_window_destroy(client->mEGLWindow);
  zwlr_layer_surface_v1_destroy(surface);
  wl_surface_destroy(client->mSurface);
}

void TCCBluescreenClient::registry_global(void *data,
                                          struct wl_registry *wl_registry,
                                          uint32_t name, const char *interface,
                                          uint32_t version) {
  TCCBluescreenClient *client = (TCCBluescreenClient *)data;

  std::string inter = interface;
  if (inter == wl_compositor_interface.name) {
    client->mCompositor = (wl_compositor *)wl_registry_bind(
        client->mRegistry, name, &wl_compositor_interface, version);

    client->mSurface = wl_compositor_create_surface(client->mCompositor);
  } else if (inter == zwlr_layer_shell_v1_interface.name) {
    client->mLayerShell = (zwlr_layer_shell_v1 *)wl_registry_bind(
        client->mRegistry, name, &zwlr_layer_shell_v1_interface, version);

    client->mLayerSurface = zwlr_layer_shell_v1_get_layer_surface(
        client->mLayerShell, client->mSurface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "tcc-bluescreen");
    zwlr_layer_surface_v1_set_margin(client->mLayerSurface, 0, 0, 0, 0);
    zwlr_layer_surface_v1_set_size(
        client->mLayerSurface, client->mOutput->width, client->mOutput->height);

    zwlr_layer_surface_v1_add_listener(client->mLayerSurface,
                                       &client->mLayerSurfaceListener, client);

    wl_surface_commit(client->mSurface);
    client->setup_egl();
  }
};
void TCCBluescreenClient::global_remove(void *data,
                                        struct wl_registry *wl_registry,
                                        uint32_t name) {};

static void release_pointer(wl_pointer *pointer) {
  if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION)
    wl_pointer_release(pointer);
  else
    wl_pointer_destroy(pointer);
}

TCCBluescreenClient::TCCBluescreenClient(
    TCCClient::Output *output, std::vector<std::string> stacktrace,
    std::vector<std::pair<std::string, std::string>> registers)
    : mOutput(output), mStacktrace(stacktrace), mRegisters(registers) {
  mDisplay = wl_display_connect(NULL);
  mRegistry = wl_display_get_registry(mDisplay);
  wl_registry_add_listener(mRegistry, &mRegistryListener, this);
  if (wl_display_roundtrip(mDisplay) == -1) {
    fprintf(stderr, "roundtrip failed\n");
    raise(SIGTRAP);
    return;
  }

  mGlyphManager.set_text_size(24);
}
void TCCBluescreenClient::run() {
  while (seconds() < 15) {
    if (wl_display_dispatch_pending(mDisplay) < 0) {
      fprintf(stderr, "dispatch failed\n");
      raise(SIGTRAP);
    }

    egl_draw();
  }
  exit(EXIT_FAILURE);
}

void TCCBluescreenClient::setup_egl() {
  const char *extensions;

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
                             EGL_DEPTH_SIZE,
                             24,
                             EGL_NONE};
  EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION,
                             1,
                             EGL_CONTEXT_MAJOR_VERSION,
                             1,
                             EGL_CONTEXT_MINOR_VERSION,
                             1,
                             EGL_NONE};
  EGLint major, minor, n, count, i, size;
  EGLConfig *configs;
  EGLBoolean ret;

  mEGLDisplay = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, mDisplay, NULL);

  ret = eglInitialize(mEGLDisplay, &major, &minor);
  assert(ret == EGL_TRUE);

  if (!eglGetConfigs(mEGLDisplay, NULL, 0, &count) || count < 1)
    assert(0);

  configs = (EGLConfig *)calloc(count, sizeof *configs);
  assert(configs);

  ret = eglChooseConfig(mEGLDisplay, config_attribs, configs, count, &n);
  assert(ret && n >= 1);

  mEGLConfig = configs[0];

  free(configs);

  mEGLWindow = wl_egl_window_create(mSurface, mOutput->width, mOutput->height);
  if (!mEGLWindow) {
    printf("ERROR: eglCreateWindowSurface, %0X\n", eglGetError());
    raise(SIGTRAP);
  }

  ret = eglBindAPI(EGL_OPENGL_API);
  assert(ret == EGL_TRUE);
  mEGLContext =
      eglCreateContext(mEGLDisplay, mEGLConfig, EGL_NO_CONTEXT, contextAttribs);
  assert(mEGLContext);

  mEGLSurface =
      eglCreatePlatformWindowSurface(mEGLDisplay, mEGLConfig, mEGLWindow, NULL);
  if (mEGLSurface == EGL_NO_SURFACE) {
    printf("eglCreatePlatformWindowSurface error: %0X\n", eglGetError());
    raise(SIGTRAP);
  }

  if (!eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext)) {
    printf("eglMakeCurrent error (init) %08X\n", eglGetError());
    raise(SIGTRAP);
  };

  eglSwapInterval(mEGLDisplay, 0);

  mClock = std::chrono::system_clock::now();
}

void TCCBluescreenClient::draw_text() {
  int x = 16;
  int y = 52;

  mGlyphManager.set_text_size(48);
  mGlyphManager.draw_text("THE CRYSTAL HAS BEEN BROKEN", x, y, mOutput->width,
                          mOutput->height, true, false);

  y += 32;
  mGlyphManager.set_text_size(24);
  mGlyphManager.draw_text("(the window manager crashed)", x, y, mOutput->width,
                          mOutput->height, true, false);
  y += 32;
  mGlyphManager.draw_text(
      std::format("(message disappears in {} seconds)", 15 - seconds()), x, y,
      mOutput->width, mOutput->height, true, false);
  y += 64;

  mGlyphManager.set_text_size(24);
  for (auto text : mStacktrace) {
    mGlyphManager.draw_text(text, x, y, mOutput->width, mOutput->height, false,
                            false);
    y += 32;
  }

  y += 32;
  mGlyphManagerTwo.set_text_size(16);
  int i = 0;
  for (auto text : mRegisters) {
    mGlyphManagerTwo.draw_text(text.first, x, y, mOutput->width,
                               mOutput->height, false, false);
    mGlyphManagerTwo.draw_text(text.second, x + 128, y, mOutput->width,
                               mOutput->height, false, false);
    if ((i & 1) == 1) {
      y += 24;
      x = 16;

    } else {
      x += 384;
    }
    i++;
  }
}

void TCCBluescreenClient::egl_draw() {
  if (eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext) !=
      EGL_TRUE) {
    printf("eglMakeCurrent error %08X\n", eglGetError());
    raise(SIGTRAP);
  };

  glViewport(0, 0, mOutput->width, mOutput->height);

  glClearColor(0.06f, 0.06f, .8f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  draw_text();

  if (eglSwapBuffers(mEGLDisplay, mEGLSurface) != EGL_TRUE) {
    printf("eglSwapBuffers error %08X\n", eglGetError());
    raise(SIGTRAP);
  };
}

TCCBluescreenClient::~TCCBluescreenClient() {
  if (!eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext)) {
    printf("eglMakeCurrent error (init) %08X\n", eglGetError());
    raise(SIGTRAP);
  };

  for (auto glyph : mGlyphManager.glyphs()) {
    glyph->destroy();
  }

  wl_egl_window_destroy(mEGLWindow);
  eglDestroyContext(mEGLDisplay, mEGLContext);
  eglDestroyContext(mEGLDisplay, mEGLSurface);
}
