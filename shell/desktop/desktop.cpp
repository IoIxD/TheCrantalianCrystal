#include "desktop.hpp"
#include <assert.h>
#include <csignal>

#include "bg_image.c"

#include "../utils/texture.hpp"
#include <EGL/eglext.h>

void TCCDesktopClient::layer_surface_configure(
    void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t w, uint32_t h) {
  TCCDesktopClient *client = (TCCDesktopClient *)data;
  /* ignore the width and height set we fucken damn well know what we're setting
   * it to.) */
  zwlr_layer_surface_v1_ack_configure(surface, serial);
}
void TCCDesktopClient::layer_surface_closed(
    void *data, struct zwlr_layer_surface_v1 *surface) {
  TCCDesktopClient *client = (TCCDesktopClient *)data;
  eglDestroySurface(client->mEGLDisplay, client->mEGLSurface);
  wl_egl_window_destroy(client->mEGLWindow);
  zwlr_layer_surface_v1_destroy(surface);
  wl_surface_destroy(client->mSurface);
}

void TCCDesktopClient::registry_global(void *data,
                                       struct wl_registry *wl_registry,
                                       uint32_t name, const char *interface,
                                       uint32_t version) {
  TCCDesktopClient *client = (TCCDesktopClient *)data;

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
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "tcc-desktop");
    zwlr_layer_surface_v1_set_margin(client->mLayerSurface, 0, 0, 0, 0);
    zwlr_layer_surface_v1_set_size(
        client->mLayerSurface, client->mOutput->width, client->mOutput->height);

    zwlr_layer_surface_v1_add_listener(client->mLayerSurface,
                                       &client->mLayerSurfaceListener, client);

    wl_surface_commit(client->mSurface);
    // wl_display_roundtrip(client->mDisplay);

    client->setup_egl();
  }
};
void TCCDesktopClient::global_remove(void *data,
                                     struct wl_registry *wl_registry,
                                     uint32_t name) {};

TCCDesktopClient::TCCDesktopClient(TCCClient::Output *output)
    : mOutput(output) {
  mDisplay = wl_display_connect(NULL);
  mRegistry = wl_display_get_registry(mDisplay);
  wl_registry_add_listener(mRegistry, &mRegistryListener, this);
  if (wl_display_roundtrip(mDisplay) == -1) {
    fprintf(stderr, "roundtrip failed\n");
    raise(SIGTRAP);
    return;
  }
}
void TCCDesktopClient::step() {
  if (wl_display_dispatch_pending(mDisplay) < 0) {
    fprintf(stderr, "dispatch failed\n");
    exit(1);
  }

  egl_draw();
}

void TCCDesktopClient::setup_egl() {
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

  /* desktop */
  mTexture = TextureManager::NewGLTextureID(
      gDesktopImage.width, gDesktopImage.height, gDesktopImage.pixel_data);
}

void TCCDesktopClient::egl_draw() {
  if (eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext) !=
      EGL_TRUE) {
    printf("eglMakeCurrent error %08X\n", eglGetError());
    raise(SIGTRAP);
  };

  zwlr_layer_surface_v1_set_margin(mLayerSurface, 0, 0, 0, 0);
  zwlr_layer_surface_v1_set_size(mLayerSurface, mOutput->width,
                                 mOutput->height);
  wl_egl_window_resize(mEGLWindow, mOutput->width, mOutput->height, 0, 0);
  glViewport(0, 0, mOutput->width, mOutput->height);

  glClearColor(1.f, 0.0f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, mTexture);
  glColor3f(1.0f, 1.0f, 1.0f);

  // todo: merge this into a field in the window instead of calculating it every
  // time.
  float screenAspect = (float)mOutput->width / (float)mOutput->height;
  float imageAspect = (float)gDesktopImage.width / (float)gDesktopImage.height;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  if (imageAspect > screenAspect) {
    scaleX = imageAspect / screenAspect;
  } else {
    scaleY = screenAspect / imageAspect;
  }

  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 1.0f);
  glVertex3f(-scaleX, -scaleY, 1); // bottom-left
  glTexCoord2f(1.0f, 1.0f);
  glVertex3f(scaleX, -scaleY, 1);
  glTexCoord2f(1.0f, 0.0f);
  glVertex3f(scaleX, scaleY, 1);
  glTexCoord2f(0.0f, 0.0f);
  glVertex3f(-scaleX, scaleY, 1); // top-right
  glEnd();

  if (eglSwapBuffers(mEGLDisplay, mEGLSurface) != EGL_TRUE) {
    printf("eglSwapBuffers error %08X\n", eglGetError());
    raise(SIGTRAP);
  };
}
