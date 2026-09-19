#include "desktop.hpp"
#include <assert.h>
#include <csignal>

#include "bg_image.c"

#include <EGL/eglext.h>

void TCCDesktopClient::layer_surface_configure(
    void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t w, uint32_t h) {
  TCCDesktopClient *client = (TCCDesktopClient *)data;
  // client->mWidth = w;
  // client->mHeight = h;
  // if (client->mEGLWindow) {
  // wl_egl_window_resize(client->mEGLWindow, w, h, 0, 0);
  // }
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

void TCCDesktopClient::output_geometry(void *data, struct wl_output *wl_output,
                                       int32_t x, int32_t y,
                                       int32_t physical_width,
                                       int32_t physical_height,
                                       int32_t subpixel, const char *make,
                                       const char *model, int32_t transform) {

};
void TCCDesktopClient::output_mode(void *data, struct wl_output *wl_output,
                                   uint32_t flags, int32_t width,
                                   int32_t height, int32_t refresh) {
  TCCDesktopClient *client = (TCCDesktopClient *)data;

  client->mWidth = width;
  client->mHeight = height;
};

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
  } else if (inter == wl_output_interface.name) {
    client->mOutput = (wl_output *)wl_registry_bind(client->mRegistry, name,
                                                    &wl_output_interface, 1);
    wl_output_add_listener(client->mOutput, &client->mOutputListener, client);
    assert(client->mOutput);
  } else if (inter == zwlr_layer_shell_v1_interface.name) {
    client->mLayerShell = (zwlr_layer_shell_v1 *)wl_registry_bind(
        client->mRegistry, name, &zwlr_layer_shell_v1_interface, version);

    client->mLayerSurface = zwlr_layer_shell_v1_get_layer_surface(
        client->mLayerShell, client->mSurface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "tcc-desktop");
    zwlr_layer_surface_v1_set_margin(client->mLayerSurface, 0, 0, 0, 0);
    zwlr_layer_surface_v1_set_size(client->mLayerSurface, client->mWidth,
                                   client->mHeight);

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

TCCDesktopClient::TCCDesktopClient(std::shared_ptr<TCCClient> client)
    : mClient(client) {
  mDisplay = wl_display_connect(NULL);
  mRegistry = wl_display_get_registry(mDisplay);
  wl_registry_add_listener(mRegistry, &mRegistryListener, this);
  if (wl_display_roundtrip(mDisplay) == -1) {
    fprintf(stderr, "roundtrip failed\n");
    raise(SIGTRAP);
    return;
  }
}
void TCCDesktopClient::run() {
  while (true) {
    if (wl_display_dispatch_pending(mDisplay) < 0) {
      fprintf(stderr, "dispatch failed\n");
      exit(1);
    }

    egl_draw();
  }
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

  mEGLWindow = wl_egl_window_create(mSurface, mWidth, mHeight);
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
  glGenTextures(1, &mTexture);
  glBindTexture(GL_TEXTURE_2D, mTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gDesktopImage.width,
               gDesktopImage.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               gDesktopImage.pixel_data);
}

void TCCDesktopClient::egl_draw() {
  if (eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext) !=
      EGL_TRUE) {
    printf("eglMakeCurrent error %08X\n", eglGetError());
    raise(SIGTRAP);
  };

  zwlr_layer_surface_v1_set_margin(mLayerSurface, 0, 0, 0, 0);
  zwlr_layer_surface_v1_set_size(mLayerSurface, mWidth, mHeight);
  wl_egl_window_resize(mEGLWindow, mWidth, mHeight, 0, 0);
  glViewport(0, 0, mWidth, mHeight);

  glClearColor(1.f, 0.0f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, mTexture);
  glColor3f(1.0f, 1.0f, 1.0f);

  // Instead of cropping the texture coordinates, overscale the quad's
  // geometry on whichever axis the image is relatively narrower on. The
  // excess falls outside the [-1, 1] clip volume and is discarded by the
  // rasterizer, giving the same aspect-correct "cover" zoom as a texcoord
  // crop would, but driven from the vertices.
  float screenAspect = (float)mWidth / (float)mHeight;
  float imageAspect = (float)gDesktopImage.width / (float)gDesktopImage.height;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  if (imageAspect > screenAspect) {
    scaleX = imageAspect / screenAspect;
  } else {
    scaleY = screenAspect / imageAspect;
  }

  // Texture coordinates are flipped vertically (v swapped between the
  // top and bottom vertices) because the decoded pixel data is stored
  // top-to-bottom while GL samples textures bottom-to-top, so this is
  // what makes the image appear right-side up on screen.
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
