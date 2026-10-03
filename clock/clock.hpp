#pragma once

#include "capture.hpp"

// (before EGL's, whose wayland-egl.h would otherwise bring in libwayland's own
// protocol functions, which aren't loaded)
#include "wayland-client-protocol.hpp"
#include "wlr-layer-shell-unstable-v1-protocol.hpp"

#include "egl_loader.hpp"
#include "freetype_loader.hpp"
#include "gl_loader.hpp"

#include <csignal>
#include <cstdint>
#include <ctime>

namespace clock_config {
// Where the clock goes on its output and how big it is, in logical pixels.
constexpr int WINDOW_X = 50, WINDOW_Y = 50;
constexpr int WINDOW_WIDTH = 250, WINDOW_HEIGHT = 300;
// The strip along the top with the date (on the left) and time (on the
// right); the face is the square below it.
constexpr int HEADER_HEIGHT = WINDOW_HEIGHT - WINDOW_WIDTH;
// How much more of what's behind the clock is captured on each side, for the
// frost to scatter in from.
constexpr int BACKDROP_MARGIN = 24;
// How far the frost scatters at full strength, in logical pixels.
constexpr float FROST_RADIUS = 14;
// How rounded the window's corners are, in logical pixels.
constexpr float CORNER_RADIUS = 25;
// What the glass is tinted, and the text's color.
constexpr float TINT = 0xD2 / 255.0f, TEXT_COLOR = 0;
// How thick the hands are, in logical pixels.
constexpr float HOUR_HAND_WIDTH = 7, MINUTE_HAND_WIDTH = 5;
// The circle around the face: its radius (in GL units, the face being 2
// across) and its width in logical pixels.
constexpr float RING_RADIUS = 0.85f, RING_WIDTH = MINUTE_HAND_WIDTH;
// How long the frost takes to fade in, or back out, in seconds.
constexpr double FADE_TIME = 0.25;
// The date and time's size, in logical pixels.
constexpr int TEXT_SIZE = 15;
// How blurred the face and text are as they start fading in (and finish
// fading out), as the gaussian's sigma in logical pixels.
constexpr double MAX_BLUR = 6;
} // namespace clock_config

/*
 * The clock that's shown while its hotkey is held: a frosted glass panel in
 * the top left of the main output with the date, the time and an analog face
 * on it, fading in when it starts and back out when it gets SIGTERM.
 *
 * clock.cpp has its timing and the main loop, wayland.cpp its surface and
 * render.cpp its drawing.
 */
class ClockWindow {
public:
  // output_name is the wl_output global of the output to go on, or 0 for the
  // first.
  ClockWindow(uint32_t output_name);
  ~ClockWindow();

  bool setup();
  void run();

private:
  // A line of text, rendered into an alpha texture.
  struct Text {
    char string[32] = "";
    GLuint texture = 0;
    // The texture's size, and how far the text itself goes across it (from
    // TEXT_PADDING in), in buffer pixels.
    int width = 0, height = 0, advance = 0;
  };

  // === clock.cpp ===

  // How frosted the glass should be right now, from 0 to 1. finished is set
  // once it's faded back out after being asked to quit.
  double frost_strength(bool *finished);
  // How far the face and text have faded in, going along with the frost.
  double shown(double strength) const;
  // How blurred they are, at that, as sigma in buffer pixels.
  double blur_sigma(double shown) const;
  bool fading() const;
  // Writes out the date and time of t, true if they've changed.
  bool update_time(time_t t);

  static void quit_handler(int);
  static volatile sig_atomic_t sQuitRequested;

  // === wayland.cpp ===

  bool setup_surface();
  bool setup_egl();
  // Handles whatever events have come in, waiting up to timeout_ms for some
  // first (-1 for as long as it takes).
  bool dispatch(int timeout_ms);
  void swap();
  void teardown_surface();

  static void registry_global(void *data, wl_registry *registry, uint32_t name,
                              const char *interface, uint32_t version);
  static void registry_global_remove(void *data, wl_registry *registry,
                                     uint32_t name);
  static void output_geometry(void *data, wl_output *output, int32_t x,
                              int32_t y, int32_t physical_width,
                              int32_t physical_height, int32_t subpixel,
                              const char *make, const char *model,
                              int32_t transform);
  static void output_mode(void *data, wl_output *output, uint32_t flags,
                          int32_t width, int32_t height, int32_t refresh);
  static void output_done(void *data, wl_output *output);
  static void output_scale(void *data, wl_output *output, int32_t factor);
  static void layer_surface_configure(void *data,
                                      zwlr_layer_surface_v1 *layer_surface,
                                      uint32_t serial, uint32_t width,
                                      uint32_t height);
  static void layer_surface_closed(void *data,
                                   zwlr_layer_surface_v1 *layer_surface);

  const wl_registry_listener mRegistryListener = {
      .global = registry_global,
      .global_remove = registry_global_remove,
  };
  // (the output's bound at version 2, so nothing past these comes)
  const wl_output_listener mOutputListener = {
      .geometry = output_geometry,
      .mode = output_mode,
      .done = output_done,
      .scale = output_scale,
  };
  const zwlr_layer_surface_v1_listener mLayerSurfaceListener = {
      .configure = layer_surface_configure,
      .closed = layer_surface_closed,
  };

  // === render.cpp ===

  bool setup_gl();
  bool setup_font();
  void teardown_gl();
  // Renders text into its texture.
  void render_text(Text *text);
  void draw(double strength);
  // What's behind everything: the frosted backdrop, cut to the window's
  // rounded corners.
  void draw_background(double strength);
  // The text, ring and hands, over whatever's there.
  void draw_foreground();
  void draw_text(const Text &text, int x, int y, int w, int h);
  void draw_ring(double radius, float width, int size);
  void draw_hand(double rad, double length, float width, int size);
  // Fills the viewport with the layer texture, blurred along (dx, dy).
  void draw_layer(float dx, float dy, double sigma, double opacity);
  // Takes the foreground drawn on its own in the frame, blurs it and puts it
  // back over the background, faded by how far it's shown.
  void blur_foreground(double strength);

  uint32_t mOutputName;

  // === Wayland ===

  wl_display *mDisplay = nullptr;
  wl_registry *mRegistry = nullptr;
  wl_compositor *mCompositor = nullptr;
  zwlr_layer_shell_v1 *mLayerShell = nullptr;
  wl_output *mOutput = nullptr;
  int mScale = 1;

  wl_surface *mSurface = nullptr;
  zwlr_layer_surface_v1 *mLayerSurface = nullptr;
  bool mConfigured = false, mClosed = false;

  wl_egl_window *mEGLWindow = nullptr;
  EGLDisplay mEGLDisplay = EGL_NO_DISPLAY;
  EGLContext mEGLContext = EGL_NO_CONTEXT;
  EGLSurface mEGLSurface = EGL_NO_SURFACE;

  // === Drawing ===

  // What was behind the clock before it showed up (or just the tint, if it
  // couldn't be captured).
  Backdrop mBackdrop;
  bool mHaveBackdrop = false;
  GLuint mBackdropTexture = 0;
  GLuint mFrostProgram = 0;
  // For blurring the foreground as it fades: the shader, and the foreground
  // on its own.
  GLuint mBlurProgram = 0, mLayerTexture = 0;

  FT_Library mFreetype = nullptr;
  FT_Face mFont = nullptr;
  Text mDate, mTime;
  struct tm mTm = {};

  // === Timing ===

  time_t mLastSecond = 0;
  // When the frost started fading in, and out (negative until then).
  double mFadeInStart = -1, mFadeOutStart = -1;
  // How frosted it was when it started fading out.
  double mFadeOutFrom = 0;
  // How frosted the last frame was, negative before there's been one.
  double mStrength = -1;
  // Something besides the frost has changed since the last frame.
  bool mChanged = true;
};
