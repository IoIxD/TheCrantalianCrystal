#include "capture.hpp"
#include "clipboard.hpp"
#include "dynload.hpp"

// (before EGL's, whose wayland-egl.h would otherwise bring in libwayland's own
// protocol functions, which aren't loaded)
#include "cursor-shape-v1-protocol.hpp"
#include "wayland-client-protocol.hpp"
#include "wlr-layer-shell-unstable-v1-protocol.hpp"

#include "egl_loader.hpp"
#include "gl_loader.hpp"

#include "frost_shader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

// How far the frost scatters at full strength, in logical pixels.
constexpr float FROST_RADIUS = 14;
// How long the frost takes to fade in, or back out, in seconds.
constexpr double FADE_TIME = 0.5;
// Selections smaller than this (in logical pixels) are taken as clicks, which
// cancel.
constexpr int MIN_SELECTION = 3;
// What the glass is tinted, the same as the clock's (Milsko's default
// background, #D2D2D2).
constexpr float TINT = 0xD2 / 255.0f;

static struct {
  wl_display *display = nullptr;
  wl_compositor *compositor = nullptr;
  zwlr_layer_shell_v1 *layer_shell = nullptr;
  wp_cursor_shape_manager_v1 *cursor_shape_manager = nullptr;
  wl_seat *seat = nullptr;
  wl_pointer *pointer = nullptr;
  wl_keyboard *keyboard = nullptr;
  wp_cursor_shape_device_v1 *cursor_shape = nullptr;

  // The output the snip's on, and how many of its pixels make a logical one.
  uint32_t output_name = 0;
  wl_output *output = nullptr;
  int scale = 1;

  wl_surface *surface = nullptr;
  zwlr_layer_surface_v1 *layer_surface = nullptr;
  // The surface's size, in logical pixels.
  int width = 0, height = 0;
  bool configured = false, closed = false;

  wl_egl_window *egl_window = nullptr;
  EGLDisplay egl_display = EGL_NO_DISPLAY;
  EGLContext egl_context = EGL_NO_CONTEXT;
  EGLSurface egl_surface = EGL_NO_SURFACE;

  // What was on the screen before the snip showed up.
  Backdrop backdrop;
  GLuint texture = 0, program = 0;

  // Where the selection started and where it's dragged to, in logical pixels.
  bool selecting = false;
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  // Where the pointer is, likewise.
  int px = 0, py = 0;
  bool changed = true;

  // When the frost started fading in, and out (negative until then).
  double fade_in_start = -1, fade_out_start = -1;
  // How frosted it was when it started fading out.
  double fade_out_from = 0;
  bool quitting = false;
  int exit_code = 0;
  // How frosted the last frame was, negative to redraw it.
  double strength = -1;
} ctx;

static double now() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static double ease(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

// How frosted the glass should be right now. finished is set once it's faded
// back out after being asked to quit.
static double frost_strength(bool *finished) {
  double t = now();
  *finished = false;

  if (ctx.fade_in_start < 0) {
    ctx.fade_in_start = t;
  }
  double strength = ease((t - ctx.fade_in_start) / FADE_TIME);

  if (ctx.quitting && ctx.fade_out_start < 0) {
    ctx.fade_out_start = t;
    ctx.fade_out_from = strength;
  }
  if (ctx.fade_out_start >= 0) {
    // (it takes less time to fade out from part way in)
    double length = FADE_TIME * ctx.fade_out_from;
    double done = length > 0 ? (t - ctx.fade_out_start) / length : 1;
    *finished = done >= 1;
    strength = ctx.fade_out_from * (1 - ease(done));
  }
  return strength;
}

// Whether the frost is fading in or out, so needs drawing every frame.
static bool fading() {
  return ctx.quitting || now() - ctx.fade_in_start < FADE_TIME;
}

// Fades out, then quits with code. Whatever was being selected is let go of,
// unless it's what's been copied.
static void quit(int code) {
  if (!ctx.quitting) {
    ctx.quitting = true;
    ctx.exit_code = code;
    if (code != 0) {
      ctx.selecting = false;
    }
    ctx.changed = true;
  }
}

// The selection, in logical pixels, put the right way round.
static void selection(int *x, int *y, int *w, int *h) {
  *x = std::min(ctx.x0, ctx.x1);
  *y = std::min(ctx.y0, ctx.y1);
  *w = std::abs(ctx.x1 - ctx.x0);
  *h = std::abs(ctx.y1 - ctx.y0);
}

// Copies the selection, at the output's own resolution, to the clipboard.
static bool copy_selection() {
  const Backdrop &b = ctx.backdrop;
  double sx = (double)b.width / ctx.width;
  double sy = (double)b.height / ctx.height;
  int x, y, w, h;
  selection(&x, &y, &w, &h);

  int px = std::clamp((int)(x * sx), 0, b.width - 1);
  int py = std::clamp((int)(y * sy), 0, b.height - 1);
  int pw = std::clamp((int)((x + w) * sx), px + 1, b.width) - px;
  int ph = std::clamp((int)((y + h) * sy), py + 1, b.height) - py;

  std::vector<unsigned char> pixels((size_t)pw * ph * 4);
  for (int row = 0; row < ph; row++) {
    memcpy(&pixels[(size_t)row * pw * 4],
           &b.pixels[((size_t)(py + row) * b.width + px) * 4], pw * 4);
  }
  return copy_png_to_clipboard(encode_png(pixels.data(), pw, ph));
}

// === Input ===

static void pointer_enter(void *, wl_pointer *pointer, uint32_t serial,
                          wl_surface *, wl_fixed_t x, wl_fixed_t y) {
  if (ctx.cursor_shape_manager && !ctx.cursor_shape) {
    ctx.cursor_shape = wp_cursor_shape_manager_v1_get_pointer(
        ctx.cursor_shape_manager, pointer);
  }
  if (ctx.cursor_shape) {
    wp_cursor_shape_device_v1_set_shape(
        ctx.cursor_shape, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR);
  }
  ctx.px = wl_fixed_to_int(x);
  ctx.py = wl_fixed_to_int(y);
}

static void pointer_leave(void *, wl_pointer *, uint32_t, wl_surface *) {}

static void pointer_motion(void *, wl_pointer *, uint32_t, wl_fixed_t x,
                           wl_fixed_t y) {
  ctx.px = wl_fixed_to_int(x);
  ctx.py = wl_fixed_to_int(y);
  if (ctx.selecting && !ctx.quitting) {
    ctx.x1 = ctx.px;
    ctx.y1 = ctx.py;
    ctx.changed = true;
  }
}

static void pointer_button(void *, wl_pointer *, uint32_t, uint32_t,
                           uint32_t button, uint32_t state) {
  if (ctx.quitting) {
    return;
  }
  bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;

  if (button == BTN_RIGHT && pressed) {
    quit(1);
  } else if (button == BTN_LEFT && pressed) {
    ctx.selecting = true;
    ctx.x0 = ctx.x1 = ctx.px;
    ctx.y0 = ctx.y1 = ctx.py;
    ctx.changed = true;
  } else if (button == BTN_LEFT && ctx.selecting) {
    int x, y, w, h;
    selection(&x, &y, &w, &h);
    if (w < MIN_SELECTION || h < MIN_SELECTION) {
      quit(1);
    } else {
      quit(copy_selection() ? 0 : 1);
    }
  }
}

static void pointer_axis(void *, wl_pointer *, uint32_t, uint32_t,
                         wl_fixed_t) {}

// (the seat's bound at version 1, so nothing past these comes)
static const wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

static void keyboard_keymap(void *, wl_keyboard *, uint32_t, int32_t fd,
                            uint32_t) {
  close(fd);
}

static void keyboard_enter(void *, wl_keyboard *, uint32_t, wl_surface *,
                           wl_array *) {}

static void keyboard_leave(void *, wl_keyboard *, uint32_t, wl_surface *) {}

static void keyboard_key(void *, wl_keyboard *, uint32_t, uint32_t,
                         uint32_t key, uint32_t state) {
  if (key == KEY_ESC && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    quit(1);
  }
}

static void keyboard_modifiers(void *, wl_keyboard *, uint32_t, uint32_t,
                               uint32_t, uint32_t, uint32_t) {}

static const wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
};

static void seat_capabilities(void *, wl_seat *seat, uint32_t caps) {
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ctx.pointer) {
    ctx.pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(ctx.pointer, &pointer_listener, NULL);
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !ctx.keyboard) {
    ctx.keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(ctx.keyboard, &keyboard_listener, NULL);
  }
}

static const wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
};

// === The surface ===

static void output_geometry(void *, wl_output *, int32_t, int32_t, int32_t,
                            int32_t, int32_t, const char *, const char *,
                            int32_t) {}

static void output_mode(void *, wl_output *, uint32_t, int32_t, int32_t,
                        int32_t) {}

static void output_done(void *, wl_output *) {}

static void output_scale(void *, wl_output *, int32_t factor) {
  ctx.scale = std::max(factor, 1);
}

// (the output's bound at version 2, so nothing past these comes)
static const wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};

static void layer_surface_configure(void *, zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
  zwlr_layer_surface_v1_ack_configure(surface, serial);
  if ((int)w != ctx.width || (int)h != ctx.height) {
    ctx.width = w;
    ctx.height = h;
    if (ctx.egl_window) {
      wl_egl_window_resize(ctx.egl_window, w * ctx.scale, h * ctx.scale, 0, 0);
    }
    ctx.changed = true;
  }
  ctx.configured = true;
}

static void layer_surface_closed(void *, zwlr_layer_surface_v1 *) {
  ctx.closed = true;
}

static const zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void registry_global(void *, wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    // (3 at least for buffer scales)
    ctx.compositor = (wl_compositor *)wl_registry_bind(
        registry, name, &wl_compositor_interface, std::min(version, 4u));
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    ctx.layer_shell = (zwlr_layer_shell_v1 *)wl_registry_bind(
        registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u));
  } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) ==
             0) {
    ctx.cursor_shape_manager = (wp_cursor_shape_manager_v1 *)wl_registry_bind(
        registry, name, &wp_cursor_shape_manager_v1_interface, 1);
  } else if (strcmp(interface, wl_seat_interface.name) == 0 && !ctx.seat) {
    ctx.seat =
        (wl_seat *)wl_registry_bind(registry, name, &wl_seat_interface, 1);
    wl_seat_add_listener(ctx.seat, &seat_listener, NULL);
  } else if (strcmp(interface, wl_output_interface.name) == 0 && !ctx.output &&
             (ctx.output_name == 0 || ctx.output_name == name)) {
    ctx.output = (wl_output *)wl_registry_bind(registry, name,
                                               &wl_output_interface, 2);
    wl_output_add_listener(ctx.output, &output_listener, NULL);
  }
}

static void registry_global_remove(void *, wl_registry *, uint32_t) {}

static const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// Puts a layer surface over the whole output, above everything (panels
// included), with the keyboard to itself so Escape always gets to it.
static bool setup_surface() {
  wl_registry *registry = wl_display_get_registry(ctx.display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  // (once for the globals, and again for the output's scale and the seat's
  // devices)
  wl_display_roundtrip(ctx.display);
  wl_display_roundtrip(ctx.display);
  if (!ctx.compositor || !ctx.layer_shell) {
    fprintf(stderr,
            "tcc_snip: wl_compositor or zwlr_layer_shell_v1 not supported\n");
    return false;
  }

  ctx.surface = wl_compositor_create_surface(ctx.compositor);
  ctx.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      ctx.layer_shell, ctx.surface, ctx.output,
      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "tcc-snip");
  zwlr_layer_surface_v1_set_anchor(
      ctx.layer_surface,
      ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
          ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_size(ctx.layer_surface, 0, 0);
  zwlr_layer_surface_v1_set_exclusive_zone(ctx.layer_surface, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      ctx.layer_surface,
      ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
  zwlr_layer_surface_v1_add_listener(ctx.layer_surface,
                                     &layer_surface_listener, NULL);
  wl_surface_set_buffer_scale(ctx.surface, ctx.scale);
  wl_surface_commit(ctx.surface);

  while (!ctx.configured && !ctx.closed) {
    if (wl_display_roundtrip(ctx.display) < 0) {
      return false;
    }
  }
  return !ctx.closed && ctx.width > 0 && ctx.height > 0;
}

static bool setup_egl() {
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
                             EGL_NONE};
  EGLint major, minor, n;
  EGLConfig config;

  ctx.egl_display =
      eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, ctx.display, NULL);
  if (ctx.egl_display == EGL_NO_DISPLAY ||
      !eglInitialize(ctx.egl_display, &major, &minor) ||
      !eglChooseConfig(ctx.egl_display, config_attribs, &config, 1, &n) ||
      n < 1 || !eglBindAPI(EGL_OPENGL_API)) {
    fprintf(stderr, "tcc_snip: couldn't set up EGL: %04X\n", eglGetError());
    return false;
  }

  ctx.egl_window = wl_egl_window_create(ctx.surface, ctx.width * ctx.scale,
                                        ctx.height * ctx.scale);
  ctx.egl_context =
      eglCreateContext(ctx.egl_display, config, EGL_NO_CONTEXT, NULL);
  if (!ctx.egl_window || ctx.egl_context == EGL_NO_CONTEXT) {
    fprintf(stderr, "tcc_snip: couldn't create a GL context: %04X\n",
            eglGetError());
    return false;
  }
  ctx.egl_surface = eglCreatePlatformWindowSurface(ctx.egl_display, config,
                                                   ctx.egl_window, NULL);
  if (ctx.egl_surface == EGL_NO_SURFACE ||
      !eglMakeCurrent(ctx.egl_display, ctx.egl_surface, ctx.egl_surface,
                      ctx.egl_context)) {
    fprintf(stderr, "tcc_snip: couldn't create an EGL surface: %04X\n",
            eglGetError());
    return false;
  }
  // (keeps the fades in step with the screen)
  eglSwapInterval(ctx.egl_display, 1);
  return true;
}

// === Drawing ===

// Uploads the backdrop and builds the frost shader.
static bool setup_frost() {
  const char *source = FROST_FRAG_SOURCE;
  GLint ok;
  GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "tcc_snip: %s\n", log);
    glDeleteShader(shader);
    return false;
  }

  ctx.program = glCreateProgram();
  glAttachShader(ctx.program, shader);
  glLinkProgram(ctx.program);
  glDeleteShader(shader);
  glGetProgramiv(ctx.program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(ctx.program, sizeof(log), NULL, log);
    fprintf(stderr, "tcc_snip: %s\n", log);
    return false;
  }

  glGenTextures(1, &ctx.texture);
  glBindTexture(GL_TEXTURE_2D, ctx.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ctx.backdrop.width,
               ctx.backdrop.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               ctx.backdrop.pixels.data());
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

// Draws the frosted backdrop over the whole surface, the selection left clear.
static void draw(double strength) {
  const float w = ctx.width, h = ctx.height;
  int sx, sy, sw, sh;
  selection(&sx, &sy, &sw, &sh);
  if (!ctx.selecting) {
    sw = sh = 0;
  }

  glViewport(0, 0, ctx.width * ctx.scale, ctx.height * ctx.scale);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glUseProgram(ctx.program);
  glBindTexture(GL_TEXTURE_2D, ctx.texture);
  glUniform1i(glGetUniformLocation(ctx.program, "backdrop"), 0);
  glUniform2f(glGetUniformLocation(ctx.program, "backdrop_size"), w, h);
  glUniform4f(glGetUniformLocation(ctx.program, "selection"), sx, sy, sw, sh);
  glUniform2f(glGetUniformLocation(ctx.program, "spread"), FROST_RADIUS / w,
              FROST_RADIUS / h);
  glUniform3f(glGetUniformLocation(ctx.program, "tint"), TINT, TINT, TINT);
  // (the lighter end of the lock screen's decorations' gradient, #1FB29C)
  glUniform3f(glGetUniformLocation(ctx.program, "border"), 0.122f, 0.698f,
              0.612f);
  glUniform1f(glGetUniformLocation(ctx.program, "strength"), strength);

  // (the backdrop's top row is first, so v goes down)
  glBegin(GL_QUADS);
  glTexCoord2f(0, 1);
  glVertex2f(-1, -1);
  glTexCoord2f(1, 1);
  glVertex2f(1, -1);
  glTexCoord2f(1, 0);
  glVertex2f(1, 1);
  glTexCoord2f(0, 0);
  glVertex2f(-1, 1);
  glEnd();

  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);

  eglSwapBuffers(ctx.egl_display, ctx.egl_surface);
}

// Handles whatever events have come in, waiting for some first if block is
// set.
static bool dispatch(bool block) {
  while (wl_display_prepare_read(ctx.display) != 0) {
    if (wl_display_dispatch_pending(ctx.display) < 0) {
      return false;
    }
  }
  wl_display_flush(ctx.display);

  pollfd pfd = {wl_display_get_fd(ctx.display), POLLIN, 0};
  if (poll(&pfd, 1, block ? -1 : 0) <= 0) {
    wl_display_cancel_read(ctx.display);
    return true;
  }
  if (wl_display_read_events(ctx.display) < 0) {
    return false;
  }
  return wl_display_dispatch_pending(ctx.display) >= 0;
}

static void teardown() {
  if (ctx.egl_display != EGL_NO_DISPLAY) {
    eglMakeCurrent(ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (ctx.egl_surface != EGL_NO_SURFACE)
      eglDestroySurface(ctx.egl_display, ctx.egl_surface);
    if (ctx.egl_context != EGL_NO_CONTEXT)
      eglDestroyContext(ctx.egl_display, ctx.egl_context);
  }
  if (ctx.egl_window)
    wl_egl_window_destroy(ctx.egl_window);
  if (ctx.egl_display != EGL_NO_DISPLAY)
    eglTerminate(ctx.egl_display);
  if (ctx.layer_surface)
    zwlr_layer_surface_v1_destroy(ctx.layer_surface);
  if (ctx.surface)
    wl_surface_destroy(ctx.surface);
  wl_display_disconnect(ctx.display);
}

// argv[1], if it's there, is the wl_output global of the output to snip.
int main(int argc, char **argv) {
  if (!dynload_setup::wayland() || !dynload_setup::egl() ||
      !dynload_setup::gl()) {
    return 1;
  }
  ctx.output_name = argc > 1 ? strtoul(argv[1], NULL, 10) : 0;

  // What's on screen has to be copied before the snip covers it.
  if (!capture_backdrop(ctx.output_name, 0, 0, 0, 0, &ctx.backdrop)) {
    return 1;
  }

  ctx.display = wl_display_connect(NULL);
  if (!ctx.display) {
    fprintf(stderr, "tcc_snip: couldn't connect to the compositor\n");
    return 1;
  }
  if (!setup_surface() || !setup_egl() || !setup_frost()) {
    teardown();
    return 1;
  }

  // Drawn whenever something's changed and every frame while fading (the swap
  // waits for the screen), and otherwise not until something happens.
  while (!ctx.closed) {
    bool finished;
    double strength = frost_strength(&finished);
    if (finished) {
      break;
    }
    if (ctx.changed || strength != ctx.strength) {
      ctx.changed = false;
      ctx.strength = strength;
      draw(strength);
    }
    if (!dispatch(!fading() && !ctx.changed)) {
      break;
    }
  }

  teardown();
  return ctx.exit_code;
}
