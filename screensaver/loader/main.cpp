/*
 * We use raw Wayland here instead of Milsko because the sacrifices I (ioi) had
 * to make to get OpenGL working with milsko (using gbm/kmsdrm for the context
 * and copying it to the widget's pixmap through the CPU) means that this is way
 * too slow under fullscreen, and would use way too much cpu.
 */

#include "wayland-client-protocol.hpp"
#include "wlr-layer-shell-unstable-v1-protocol.hpp"

#include "dynload.hpp"
#include "egl_loader.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <format>
#include <limits.h>
#include <poll.h>
#include <string>
#include <unistd.h>

struct Screensaver {
  void *lib = nullptr;
  void *(*init)() = nullptr;
  void (*draw)(void *ctx, int width, int height) = nullptr;
  void (*free)(void *ctx) = nullptr;
  void *ctx = nullptr;

  Screensaver(std::string name) {
    char dest[PATH_MAX];
    memset(dest, 0, sizeof(dest));
    if (readlink("/proc/self/exe", dest, PATH_MAX) == -1) {
      perror("readlink");
    }
    std::filesystem::path fs = dest;

    std::filesystem::path path =
        fs.parent_path() / std::format("lib{}.so", name);

    lib = dlopen(path.string().c_str(), RTLD_LAZY | RTLD_NOW);
    if (!lib) {
      printf("lib %s not found\n", path.string().c_str());
      return;
    }
    init = (decltype(init))dlsym(lib, "tcc_scr_init");
    draw = (decltype(draw))dlsym(lib, "tcc_scr_draw");
    free = (decltype(free))dlsym(lib, "tcc_scr_free");

    if (init) {
      ctx = init();
    }
  };

  ~Screensaver() {
    if (free)
      free(ctx);

    if (lib)
      dlclose(lib);
  }
};

// How far the pointer has to travel before it counts as the user waking the
// screen up, so a jittery mouse or a synthetic motion event doesn't.
constexpr double WAKE_DISTANCE = 16.0;

static struct LoaderContext {
  wl_display *display = nullptr;
  wl_registry *registry = nullptr;
  wl_compositor *compositor = nullptr;
  zwlr_layer_shell_v1 *layer_shell = nullptr;
  wl_seat *seat = nullptr;
  wl_keyboard *keyboard = nullptr;
  wl_pointer *pointer = nullptr;

  wl_surface *surface = nullptr;
  zwlr_layer_surface_v1 *layer_surface = nullptr;

  wl_egl_window *egl_window = nullptr;
  EGLDisplay egl_display = EGL_NO_DISPLAY;
  EGLContext egl_context = EGL_NO_CONTEXT;
  EGLSurface egl_surface = EGL_NO_SURFACE;

  Screensaver *scr = nullptr;
  int width = 0, height = 0;
  bool configured = false;
  bool running = true;

  bool pointer_seen = false;
  double pointer_x = 0.0, pointer_y = 0.0;

  void tick() {
    if (scr->draw)
      scr->draw(scr->ctx, width, height);

    if (eglSwapBuffers(egl_display, egl_surface) != EGL_TRUE)
      fprintf(stderr, "eglSwapBuffers error %08X\n", eglGetError());
  };

  bool setup_egl();

  static void registry_global(void *data, wl_registry *registry, uint32_t name,
                              const char *interface, uint32_t version) {
    LoaderContext *ctx = (LoaderContext *)data;

    std::string inter = interface;
    if (inter == wl_compositor_interface.name) {
      ctx->compositor = (wl_compositor *)wl_registry_bind(
          registry, name, &wl_compositor_interface, 1);
    } else if (inter == zwlr_layer_shell_v1_interface.name) {
      ctx->layer_shell = (zwlr_layer_shell_v1 *)wl_registry_bind(
          registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (inter == wl_seat_interface.name && !ctx->seat) {
      ctx->seat =
          (wl_seat *)wl_registry_bind(registry, name, &wl_seat_interface, 1);
      wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
    }
  };
  static void registry_global_remove(void *data, wl_registry *registry,
                                     uint32_t name) {};

  static void layer_surface_configure(void *data,
                                      zwlr_layer_surface_v1 *surface,
                                      uint32_t serial, uint32_t w, uint32_t h) {
    LoaderContext *ctx = (LoaderContext *)data;

    zwlr_layer_surface_v1_ack_configure(surface, serial);
    ctx->width = (int)w;
    ctx->height = (int)h;
    ctx->configured = true;

    if (ctx->egl_window)
      wl_egl_window_resize(ctx->egl_window, ctx->width, ctx->height, 0, 0);
  };
  static void layer_surface_closed(void *data, zwlr_layer_surface_v1 *surface) {
    ((LoaderContext *)data)->running = false;
  };

  static void seat_capabilities(void *data, wl_seat *seat, uint32_t caps) {
    LoaderContext *ctx = (LoaderContext *)data;

    bool has_keyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
    if (has_keyboard && !ctx->keyboard) {
      ctx->keyboard = wl_seat_get_keyboard(seat);
      wl_keyboard_add_listener(ctx->keyboard, &keyboard_listener, ctx);
    } else if (!has_keyboard && ctx->keyboard) {
      wl_keyboard_destroy(ctx->keyboard);
      ctx->keyboard = nullptr;
    }

    bool has_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (has_pointer && !ctx->pointer) {
      ctx->pointer = wl_seat_get_pointer(seat);
      wl_pointer_add_listener(ctx->pointer, &pointer_listener, ctx);
    } else if (!has_pointer && ctx->pointer) {
      wl_pointer_destroy(ctx->pointer);
      ctx->pointer = nullptr;
    }
  };
  static void seat_name(void *data, wl_seat *seat, const char *name) {};

  // Any key press ends the screensaver.
  static void keyboard_keymap(void *data, wl_keyboard *keyboard,
                              uint32_t format, int32_t fd, uint32_t size) {
    close(fd);
  };
  static void keyboard_enter(void *data, wl_keyboard *keyboard, uint32_t serial,
                             wl_surface *surface, wl_array *keys) {};
  static void keyboard_leave(void *data, wl_keyboard *keyboard, uint32_t serial,
                             wl_surface *surface) {};
  static void keyboard_key(void *data, wl_keyboard *keyboard, uint32_t serial,
                           uint32_t time, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
      ((LoaderContext *)data)->running = false;
  };
  static void keyboard_modifiers(void *data, wl_keyboard *keyboard,
                                 uint32_t serial, uint32_t depressed,
                                 uint32_t latched, uint32_t locked,
                                 uint32_t group) {};
  static void keyboard_repeat_info(void *data, wl_keyboard *keyboard,
                                   int32_t rate, int32_t delay) {};

  // So does clicking, scrolling, or moving the mouse a meaningful distance.
  static void pointer_enter(void *data, wl_pointer *pointer, uint32_t serial,
                            wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    LoaderContext *ctx = (LoaderContext *)data;

    // Hide the cursor while the screensaver is up.
    wl_pointer_set_cursor(pointer, serial, NULL, 0, 0);

    ctx->pointer_seen = true;
    ctx->pointer_x = wl_fixed_to_double(x);
    ctx->pointer_y = wl_fixed_to_double(y);
  };
  static void pointer_leave(void *data, wl_pointer *pointer, uint32_t serial,
                            wl_surface *surface) {};
  static void pointer_motion(void *data, wl_pointer *pointer, uint32_t time,
                             wl_fixed_t x, wl_fixed_t y) {
    LoaderContext *ctx = (LoaderContext *)data;

    double nx = wl_fixed_to_double(x);
    double ny = wl_fixed_to_double(y);
    if (!ctx->pointer_seen) {
      ctx->pointer_seen = true;
      ctx->pointer_x = nx;
      ctx->pointer_y = ny;
      return;
    }

    if (std::hypot(nx - ctx->pointer_x, ny - ctx->pointer_y) > WAKE_DISTANCE)
      ctx->running = false;
  };
  static void pointer_button(void *data, wl_pointer *pointer, uint32_t serial,
                             uint32_t time, uint32_t button, uint32_t state) {
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
      ((LoaderContext *)data)->running = false;
  };
  static void pointer_axis(void *data, wl_pointer *pointer, uint32_t time,
                           uint32_t axis, wl_fixed_t value) {
    ((LoaderContext *)data)->running = false;
  };

  static constexpr wl_registry_listener registry_listener = {
      .global = registry_global,
      .global_remove = registry_global_remove,
  };
  static constexpr zwlr_layer_surface_v1_listener layer_surface_listener = {
      .configure = layer_surface_configure,
      .closed = layer_surface_closed,
  };
  static constexpr wl_seat_listener seat_listener = {
      .capabilities = seat_capabilities,
      .name = seat_name,
  };
  static constexpr wl_keyboard_listener keyboard_listener = {
      .keymap = keyboard_keymap,
      .enter = keyboard_enter,
      .leave = keyboard_leave,
      .key = keyboard_key,
      .modifiers = keyboard_modifiers,
      .repeat_info = keyboard_repeat_info,
  };
  // The seat is bound at version 1, so the later pointer events never arrive.
  static constexpr wl_pointer_listener pointer_listener = {
      .enter = pointer_enter,
      .leave = pointer_leave,
      .motion = pointer_motion,
      .button = pointer_button,
      .axis = pointer_axis,
  };
} ctx;

bool LoaderContext::setup_egl() {
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
  EGLint context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 2,
                              EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
  EGLint major, minor, n;
  EGLConfig config;

  egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display, NULL);
  if (egl_display == EGL_NO_DISPLAY ||
      !eglInitialize(egl_display, &major, &minor)) {
    fprintf(stderr, "eglInitialize error %08X\n", eglGetError());
    return false;
  }

  if (!eglChooseConfig(egl_display, config_attribs, &config, 1, &n) || n < 1) {
    fprintf(stderr, "no suitable EGL config\n");
    return false;
  }

  if (!eglBindAPI(EGL_OPENGL_API)) {
    fprintf(stderr, "eglBindAPI error %08X\n", eglGetError());
    return false;
  }

  egl_context =
      eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
  if (egl_context == EGL_NO_CONTEXT) {
    fprintf(stderr, "eglCreateContext error %08X\n", eglGetError());
    return false;
  }

  egl_window = wl_egl_window_create(surface, width, height);
  if (!egl_window) {
    fprintf(stderr, "wl_egl_window_create failed\n");
    return false;
  }

  egl_surface =
      eglCreatePlatformWindowSurface(egl_display, config, egl_window, NULL);
  if (egl_surface == EGL_NO_SURFACE) {
    fprintf(stderr, "eglCreatePlatformWindowSurface error %08X\n",
            eglGetError());
    return false;
  }

  if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
    fprintf(stderr, "eglMakeCurrent error %08X\n", eglGetError());
    return false;
  }

  // Swapping waits for the compositor's next frame, which paces the main loop
  // to the display's refresh rate.
  eglSwapInterval(egl_display, 1);

  return true;
}

int main() {
  if (!dynload_setup::wayland() || !dynload_setup::egl())
    return 1;

  ctx.display = wl_display_connect(NULL);
  if (!ctx.display) {
    fprintf(stderr, "could not connect to the wayland display\n");
    return 1;
  }

  ctx.registry = wl_display_get_registry(ctx.display);
  wl_registry_add_listener(ctx.registry, &LoaderContext::registry_listener,
                           &ctx);
  wl_display_roundtrip(ctx.display);
  if (!ctx.compositor || !ctx.layer_shell) {
    fprintf(stderr, "wl_compositor or zwlr_layer_shell_v1 not supported\n");
    return 1;
  }
  // Pick up the seat's capabilities.
  wl_display_roundtrip(ctx.display);

  ctx.surface = wl_compositor_create_surface(ctx.compositor);
  ctx.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      ctx.layer_shell, ctx.surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
      "tcc-screensaver");
  zwlr_layer_surface_v1_set_anchor(ctx.layer_surface,
                                   ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_size(ctx.layer_surface, 0, 0);
  zwlr_layer_surface_v1_set_exclusive_zone(ctx.layer_surface, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      ctx.layer_surface,
      ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
  zwlr_layer_surface_v1_add_listener(
      ctx.layer_surface, &LoaderContext::layer_surface_listener, &ctx);
  wl_surface_commit(ctx.surface);

  while (!ctx.configured && ctx.running) {
    if (wl_display_roundtrip(ctx.display) == -1) {
      fprintf(stderr, "roundtrip failed\n");
      return 1;
    }
  }

  if (!ctx.running || !ctx.setup_egl())
    return 1;

  /* todo: loading custom ones */
  ctx.scr = new Screensaver("tcc_scr_tunnel");

  int fd = wl_display_get_fd(ctx.display);

  while (ctx.running) {
    ctx.tick();

    // Handle whatever input has arrived without blocking; the next swap is
    // what waits.
    while (wl_display_prepare_read(ctx.display) != 0)
      wl_display_dispatch_pending(ctx.display);
    wl_display_flush(ctx.display);

    pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
    if (poll(&pfd, 1, 0) > 0) {
      if (wl_display_read_events(ctx.display) == -1) {
        fprintf(stderr, "lost the wayland connection\n");
        break;
      }
    } else {
      wl_display_cancel_read(ctx.display);
    }

    if (wl_display_dispatch_pending(ctx.display) == -1) {
      fprintf(stderr, "dispatch failed\n");
      break;
    }
  }

  // (the screensaver may free GL objects, so it goes while the context is
  // still current.)
  delete ctx.scr;

  eglMakeCurrent(ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                 EGL_NO_CONTEXT);
  eglDestroySurface(ctx.egl_display, ctx.egl_surface);
  eglDestroyContext(ctx.egl_display, ctx.egl_context);
  wl_egl_window_destroy(ctx.egl_window);
  eglTerminate(ctx.egl_display);

  zwlr_layer_surface_v1_destroy(ctx.layer_surface);
  wl_surface_destroy(ctx.surface);
  wl_display_disconnect(ctx.display);
}
