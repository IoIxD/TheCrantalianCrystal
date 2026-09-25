#pragma once
#include "../utils/icon.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <wayland-client.h>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "../protocol/cursor-shape-v1-protocol.h"
#include "../protocol/river-input-management-v1-protocol.h"
#include "../protocol/river-layer-shell-v1-protocol.h"
#include "../protocol/river-window-management-v1-protocol.h"
#include "../protocol/river-xkb-bindings-v1-protocol.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-egl.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include "../utils/glyph.hpp"

#define SSD_BORDER_SIZE 5
#define SSD_BORDER_LEEWAY 5
#define SSD_BORDER_SIZE_TOP 32
#define SSD_BORDER_SIZE_TOTAL SSD_BORDER_SIZE_TOP + SSD_BORDER_SIZE

// Resize surfaces cover the border and extend SSD_BORDER_LEEWAY past it.
#define SSD_RESIZE_THICKNESS (SSD_BORDER_SIZE + SSD_BORDER_LEEWAY)

// Nav button hitboxes, in decoration surface coordinates. These mirror the
// button rects drawn in ssd.frag.
#define SSD_NAV_BUTTON_WIDTH 22
#define SSD_NAV_BUTTON_HEIGHT 21
#define SSD_NAV_BUTTON_Y 6
#define SSD_NAV_BUTTON_CLOSE_X_FROM_RIGHT 32
#define SSD_NAV_BUTTON_MAX_X_FROM_RIGHT 57
#define SSD_NAV_BUTTON_MIN_X_FROM_RIGHT 82

class TCCClient : public std::enable_shared_from_this<TCCClient> {
  struct Seat;

  enum Action {
    ACTION_NONE,
    ACTION_SPAWN_TERMINAL,
    ACTION_CLOSE,
    ACTION_FOCUS_NEXT,
    ACTION_MOVE,
    ACTION_RESIZE,
    ACTION_EXIT,
    ACTION_SPAWN_SIGSEGV,
  };

  enum SeatOp {
    SEAT_OP_NONE,
    SEAT_OP_MOVE,
    SEAT_OP_RESIZE,
  };

  enum NavButton {
    NAV_BUTTON_NONE = 0,
    NAV_BUTTON_CLOSE,
    NAV_BUTTON_MIN,
    NAV_BUTTON_MAX,
    NAV_BUTTON_COUNT,
  };

  enum ResizeSurface {
    RESIZE_SURFACE_TOP,
    RESIZE_SURFACE_BOTTOM,
    RESIZE_SURFACE_LEFT,
    RESIZE_SURFACE_RIGHT,
    // Corners come last so they're stacked above the edges.
    RESIZE_SURFACE_TOP_LEFT,
    RESIZE_SURFACE_TOP_RIGHT,
    RESIZE_SURFACE_BOTTOM_LEFT,
    RESIZE_SURFACE_BOTTOM_RIGHT,
    RESIZE_SURFACE_COUNT,
  };

public:
  class Window {
    GlyphManager mGlyphManager;

    wl_egl_window *mEGLWindow;
    EGLDisplay mEGLDisplay;
    EGLContext mEGLContext;
    EGLConfig mEGLConfig;
    EGLSurface mEGLSurface;
    GLuint mEGLShaderProgram;

    std::string mIconPath;

  public:
    std::shared_ptr<TCCClient> client;
    river_window_v1 *id;
    river_node_v1 *node;
    bool is_new = false;
    bool closed = false;
    int saved_x = 0, saved_y = 0;
    int saved_width = 0, saved_height = 0;

    bool hide_decor = false;
    bool has_decor = false;
    river_decoration_v1 *decor_decor;
    wl_surface *decor_surface;
    GLuint decor_icon_texture = -1;

    bool center_requested = false;

    struct nav_surface {
      wl_surface *surface = nullptr;
      wl_subsurface *subsurface = nullptr;
    };
    // Indexed by NavButton, NAV_BUTTON_NONE is unused.
    nav_surface nav_surfaces[NAV_BUTTON_COUNT];

    struct resize_surface {
      wl_surface *surface = nullptr;
      wl_subsurface *subsurface = nullptr;
      // Size of the currently attached buffer, 0 if unmapped.
      int width = 0, height = 0;
    };
    // Indexed by ResizeSurface.
    resize_surface resize_surfaces[RESIZE_SURFACE_COUNT];

    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int decor_width = 0;
    int decor_height = 0;
    // From river_window_v1.dimensions_hint, 0 means no preference.
    int32_t min_width = 0, min_height = 0;
    int32_t max_width = 0, max_height = 0;
    char title[2048];
    char app_id[4096];

    Seat *pointer_move_requested = nullptr;
    Seat *pointer_resize_requested = nullptr;
    uint32_t pointer_resize_requested_edges = 0;

    bool maximized = false;
    bool minimized = false;

    bool close_held = false;
    bool minimize_held = false;
    bool maximize_held = false;
    bool close_hover = false;
    bool minimize_hover = false;
    bool maximize_hover = false;

    bool queue_minimize = false;

    // Applied during the next manage sequence.
    uint8_t pending_nav_action = NAV_BUTTON_NONE;

    void setup_decor();
    void decor_draw();

    void decor_draw_backing();
    void decor_draw_icon();

    ~Window();
  };

  class Output : public std::enable_shared_from_this<Output> {
  public:
    std::shared_ptr<TCCClient> client;
    river_output_v1 *id;
    bool removed = false;
    int x = 0;
    int y = 0;
    int width = 10;
    int height = 10;
    std::unique_ptr<class TCCDesktopClient> desktop_client;
    std::vector<Window *> windows;
    std::vector<Window *> minimized_windows;
  };

private:
  struct XkbBinding {
    std::shared_ptr<TCCClient> client;
    river_xkb_binding_v1 *id;
    Seat *seat;
    Action action;
  };

  struct PointerBinding {
    std::shared_ptr<TCCClient> client;
    river_pointer_binding_v1 *id;
    Seat *seat;
    Action action;
  };

  struct Seat {
    std::shared_ptr<TCCClient> client;
    river_seat_v1 *id;
    bool is_new = false;
    bool removed = false;

    Window *focused = nullptr;
    Window *hovered = nullptr;
    Window *interacted = nullptr;
    Window *holding = nullptr;

    std::vector<std::shared_ptr<XkbBinding>> xkb_bindings;
    std::vector<std::shared_ptr<PointerBinding>> pointer_bindings;
    Action pending_action = ACTION_NONE;

    SeatOp op = SEAT_OP_NONE;
    // For SEAT_OP_MOVE and SEAT_OP_RESIZE
    Window *op_window = nullptr;
    int32_t op_start_x = 0, op_start_y = 0;
    int32_t op_dx = 0, op_dy = 0;
    bool op_release = false;
    // For SEAT_OP_RESIZE only
    int32_t op_start_width = 0, op_start_height = 0;
    uint32_t op_edges = 0;

    double pointer_x = 0, pointer_y = 0;

    // The wl_seat advertised by river_seat_v1.wl_seat, and its pointer.
    wl_seat *wl_seat_id = nullptr;
    wl_pointer *wl_pointer_id = nullptr;
    // Only set if the compositor supports wp_cursor_shape_manager_v1.
    wp_cursor_shape_device_v1 *cursor_shape_device = nullptr;
    // Window whose decoration surface currently has wl_pointer focus.
    Window *pointer_window = nullptr;
    // Nav button surface that currently has wl_pointer focus, and where.
    uint8_t pointer_nav_button = NAV_BUTTON_NONE;
    double pointer_nav_x = 0, pointer_nav_y = 0;
    // Edges of the resize surface that currently has wl_pointer focus.
    uint32_t pointer_resize_edges = 0;
    // Nav button the pointer was pressed on.
    uint8_t nav_button_pressed = NAV_BUTTON_NONE;
  };

  IconManager mIconManager;

  wl_display *mDisplay = nullptr;
  wl_registry *mRegistry = nullptr;
  wl_compositor *mCompositor = nullptr;
  wl_subcompositor *mSubcompositor = nullptr;
  wl_shm *mShm = nullptr;
  // Fully transparent buffer shared by every nav button surface.
  wl_buffer *mNavButtonBuffer = nullptr;
  // Zero-filled shm pool backing every transparent buffer. Only ever grows.
  int mTransparentPoolFd = -1;
  int mTransparentPoolSize = 0;
  wl_shm_pool *mTransparentPool = nullptr;

  std::vector<Output *> mOutputs;
  std::vector<Seat *> mSeats;

  // wl_seat global name -> advertised version
  std::unordered_map<uint32_t, uint32_t> mWlSeatVersions;

  bool mRunning = true;
  bool mStopping = false;

  const wl_registry_listener mRegistryListener = {
      .global = registry_global,
      .global_remove = global_remove,
  };
  static void registry_global(void *data, struct wl_registry *wl_registry,
                              uint32_t name, const char *interface,
                              uint32_t version);
  static void global_remove(void *data, struct wl_registry *wl_registry,
                            uint32_t name);
  wl_surface *mRiverWindowSurface = nullptr;

  river_window_manager_v1 *mRiverWindowManager = nullptr;
  river_input_manager_v1 *mRiverInputManager = nullptr;
  river_xkb_bindings_v1 *mRiverXKBBinding = nullptr;
  wp_cursor_shape_manager_v1 *mCursorShapeManager = nullptr;
  const river_window_manager_v1_listener mRiverWindowManagementListener = {
      .unavailable = river_wm_unavailable,
      .finished = river_wm_finished,
      .manage_start = river_wm_manage_start,
      .render_start = river_wm_render_start,
      .session_locked = river_wm_session_locked,
      .session_unlocked = river_wm_session_unlocked,
      .window = river_wm_window,
      .output = river_wm_output,
      .seat = river_wm_seat,
  };
  const river_input_manager_v1_listener mRiverInputManagementListener = {
      .finished = river_input_finished,
      .input_device = river_input_input_device,
  };

  const river_window_v1_listener mRiverWindowListener = {
      .closed = river_window_closed,
      .dimensions_hint = river_window_dimensions_hint,
      .dimensions = river_window_dimensions,
      .app_id = river_window_app_id,
      .title = river_window_title,
      .parent = river_window_parent,
      .decoration_hint = river_window_decoration_hint,
      .pointer_move_requested = river_window_pointer_move_requested,
      .pointer_resize_requested = river_window_pointer_resize_requested,
      .show_window_menu_requested = river_window_show_window_menu_requested,
      .maximize_requested = river_window_maximize_requested,
      .unmaximize_requested = river_window_unmaximize_requested,
      .fullscreen_requested = river_window_fullscreen_requested,
      .exit_fullscreen_requested = river_window_exit_fullscreen_requested,
      .minimize_requested = river_window_minimize_requested,
      .unreliable_pid = river_window_unreliable_pid,
      .presentation_hint = river_window_presentation_hint,
      .identifier = river_window_identifier,
  };

  const river_output_v1_listener mRiverOutputListener = {
      .removed = river_output_removed,
      .wl_output = river_output_wl_output,
      .position = river_output_position,
      .dimensions = river_output_dimensions,
  };

  const river_seat_v1_listener mRiverSeatListener = {
      .removed = river_seat_removed,
      .wl_seat = river_seat_wl_seat,
      .pointer_enter = river_seat_pointer_enter,
      .pointer_leave = river_seat_pointer_leave,
      .window_interaction = river_seat_window_interaction,
      .shell_surface_interaction = river_seat_shell_surface_interaction,
      .op_delta = river_seat_op_delta,
      .op_release = river_seat_op_release,
      .pointer_position = river_seat_pointer_position,
  };

  const wl_seat_listener mWlSeatListener = {
      .capabilities = wl_seat_capabilities,
      .name = wl_seat_name,
  };

  const wl_pointer_listener mWlPointerListener = {
      .enter = wl_pointer_enter,
      .leave = wl_pointer_leave,
      .motion = wl_pointer_motion,
      .button = wl_pointer_button,
      .axis = wl_pointer_axis,
  };

  const river_xkb_binding_v1_listener mRiverXkbBindingListener = {
      .pressed = river_xkb_binding_pressed,
      .released = river_xkb_binding_released,
      .stop_repeat = river_xkb_binding_stop_repeat,
  };

  const river_pointer_binding_v1_listener mRiverPointerBindingListener = {
      .pressed = river_pointer_binding_pressed,
      .released = river_pointer_binding_released,
  };

  static void
  river_wm_unavailable(void *data,
                       struct river_window_manager_v1 *river_window_manager_v1);
  static void
  river_wm_finished(void *data,
                    struct river_window_manager_v1 *river_window_manager_v1);
  static void river_wm_manage_start(
      void *data, struct river_window_manager_v1 *river_window_manager_v1);
  static void river_wm_render_start(
      void *data, struct river_window_manager_v1 *river_window_manager_v1);
  static void river_wm_session_locked(
      void *data, struct river_window_manager_v1 *river_window_manager_v1);
  static void river_wm_session_unlocked(
      void *data, struct river_window_manager_v1 *river_window_manager_v1);
  static void
  river_wm_window(void *data,
                  struct river_window_manager_v1 *river_window_manager_v1,
                  struct river_window_v1 *id);
  static void
  river_wm_output(void *data,
                  struct river_window_manager_v1 *river_window_manager_v1,
                  struct river_output_v1 *id);
  static void
  river_wm_seat(void *data,
                struct river_window_manager_v1 *river_window_manager_v1,
                struct river_seat_v1 *id);

  static void river_window_closed(void *data, struct river_window_v1 *id);
  static void
  river_window_dimensions_hint(void *data, struct river_window_v1 *id,
                               int32_t min_width, int32_t min_height,
                               int32_t max_width, int32_t max_height);
  static void river_window_dimensions(void *data, struct river_window_v1 *id,
                                      int32_t width, int32_t height);
  static void river_window_app_id(void *data, struct river_window_v1 *id,
                                  const char *app_id);
  static void river_window_title(void *data, struct river_window_v1 *id,
                                 const char *title);
  static void river_window_parent(void *data, struct river_window_v1 *id,
                                  struct river_window_v1 *parent);
  static void river_window_decoration_hint(void *data,
                                           struct river_window_v1 *id,
                                           uint32_t hint);
  static void river_window_pointer_move_requested(void *data,
                                                  struct river_window_v1 *id,
                                                  struct river_seat_v1 *seat);
  static void river_window_pointer_resize_requested(void *data,
                                                    struct river_window_v1 *id,
                                                    struct river_seat_v1 *seat,
                                                    uint32_t edges);
  static void river_window_show_window_menu_requested(
      void *data, struct river_window_v1 *id, int32_t x, int32_t y);
  static void river_window_maximize_requested(void *data,
                                              struct river_window_v1 *id);
  static void river_window_unmaximize_requested(void *data,
                                                struct river_window_v1 *id);
  static void river_window_fullscreen_requested(void *data,
                                                struct river_window_v1 *id,
                                                struct river_output_v1 *output);
  static void
  river_window_exit_fullscreen_requested(void *data,
                                         struct river_window_v1 *id);
  static void river_window_minimize_requested(void *data,
                                              struct river_window_v1 *id);
  static void river_window_unreliable_pid(void *data,
                                          struct river_window_v1 *id,
                                          int32_t unreliable_pid);
  static void river_window_presentation_hint(void *data,
                                             struct river_window_v1 *id,
                                             uint32_t hint);
  static void river_window_identifier(void *data, struct river_window_v1 *id,
                                      const char *identifier);

  static void river_output_removed(void *data, struct river_output_v1 *id);
  static void river_output_wl_output(void *data, struct river_output_v1 *id,
                                     uint32_t name);
  static void river_output_position(void *data, struct river_output_v1 *id,
                                    int32_t x, int32_t y);
  static void river_output_dimensions(void *data, struct river_output_v1 *id,
                                      int32_t width, int32_t height);

  static void river_seat_removed(void *data, struct river_seat_v1 *id);
  static void river_seat_wl_seat(void *data, struct river_seat_v1 *id,
                                 uint32_t seat_id);
  static void river_seat_pointer_enter(void *data, struct river_seat_v1 *id,
                                       struct river_window_v1 *window);
  static void river_seat_pointer_leave(void *data, struct river_seat_v1 *id);
  static void river_seat_window_interaction(void *data,
                                            struct river_seat_v1 *id,
                                            struct river_window_v1 *window);
  static void river_seat_shell_surface_interaction(
      void *data, struct river_seat_v1 *id,
      struct river_shell_surface_v1 *shell_surface);
  static void river_seat_op_delta(void *data, struct river_seat_v1 *id,
                                  int32_t dx, int32_t dy);
  static void river_seat_op_release(void *data, struct river_seat_v1 *id);
  static void river_seat_pointer_position(void *data, struct river_seat_v1 *id,
                                          int32_t x, int32_t y);

  static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat,
                                   uint32_t capabilities);
  static void wl_seat_name(void *data, struct wl_seat *wl_seat,
                           const char *name);

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

  static void
  river_xkb_binding_pressed(void *data,
                            struct river_xkb_binding_v1 *river_xkb_binding_v1);
  static void
  river_xkb_binding_released(void *data,
                             struct river_xkb_binding_v1 *river_xkb_binding_v1);
  static void river_xkb_binding_stop_repeat(
      void *data, struct river_xkb_binding_v1 *river_xkb_binding_v1);

  static void river_pointer_binding_pressed(
      void *data, struct river_pointer_binding_v1 *river_pointer_binding_v1);
  static void river_pointer_binding_released(
      void *data, struct river_pointer_binding_v1 *river_pointer_binding_v1);

  static void
  river_input_finished(void *data,
                       struct river_input_manager_v1 *river_input_manager_v1);
  static void river_input_input_device(
      void *data, struct river_input_manager_v1 *river_input_manager_v1,
      struct river_input_device_v1 *id);

  // Window management policy helpers, ported from tinyrwm.c.
  void output_maybe_destroy(Output *output);

  void window_maybe_destroy(Window *window);
  void window_set_position(Window *window, int32_t x, int32_t y);
  void window_manage(Window *window);

  void xkb_binding_create(Seat *seat, uint32_t mods, xkb_keysym_t keysym,
                          Action action);
  void xkb_binding_destroy(std::shared_ptr<XkbBinding> binding);

  void pointer_binding_create(Seat *seat, uint32_t mods, uint32_t button,
                              Action action);
  void pointer_binding_destroy(std::shared_ptr<PointerBinding> binding);

  void seat_maybe_destroy(Seat *seat);
  void seat_focus(Seat *seat, Window *window);
  void seat_pointer_move(Seat *seat, Window *window);
  void seat_pointer_resize(Seat *seat, Window *window, uint32_t edges);
  void seat_action(Seat *seat, Action action);
  void seat_manage(Seat *seat);
  void seat_render(Seat *seat);

  void nav_button_action(uint8_t action, bool released, Window *window);
  void nav_button_hover(uint8_t button, Window *window);
  uint8_t get_pressed_nav_button(Seat *seat, Window *window);

  Window *window_from_decor_surface(wl_surface *surface);
  Window *window_from_nav_surface(wl_surface *surface, uint8_t *button);
  Window *window_from_resize_surface(wl_surface *surface, uint32_t *edges);

  wl_buffer *create_transparent_buffer(int width, int height);
  wl_buffer *get_nav_button_buffer();
  void window_create_nav_surfaces(Window *window);
  void window_destroy_nav_surfaces(Window *window);
  void window_position_nav_surfaces(Window *window);
  void window_create_resize_surfaces(Window *window);
  void window_destroy_resize_surfaces(Window *window);
  void window_position_resize_surfaces(Window *window);

public:
  TCCClient();
  ~TCCClient();
  void run();
  void window_maximize(Window *window);
  void window_minimize(Window *window);

  void dirty() { river_window_manager_v1_manage_dirty(mRiverWindowManager); }

  void terminate() {
    printf("%p\n", mRiverWindowManager);
    if (mRiverWindowManager)
      river_window_manager_v1_exit_session(mRiverWindowManager);
    mRunning = false;
    wl_display_flush(mDisplay);
  }

  const std::vector<Output *> &outputs() { return mOutputs; };
};

static const int nav_button_x_from_right[] = {
    0,
    SSD_NAV_BUTTON_CLOSE_X_FROM_RIGHT,
    SSD_NAV_BUTTON_MIN_X_FROM_RIGHT,
    SSD_NAV_BUTTON_MAX_X_FROM_RIGHT,
};

// Indexed by TCCClient::ResizeSurface.
static const uint32_t resize_surface_edges[] = {
    RIVER_WINDOW_V1_EDGES_TOP,
    RIVER_WINDOW_V1_EDGES_BOTTOM,
    RIVER_WINDOW_V1_EDGES_LEFT,
    RIVER_WINDOW_V1_EDGES_RIGHT,
    RIVER_WINDOW_V1_EDGES_TOP | RIVER_WINDOW_V1_EDGES_LEFT,
    RIVER_WINDOW_V1_EDGES_TOP | RIVER_WINDOW_V1_EDGES_RIGHT,
    RIVER_WINDOW_V1_EDGES_BOTTOM | RIVER_WINDOW_V1_EDGES_LEFT,
    RIVER_WINDOW_V1_EDGES_BOTTOM | RIVER_WINDOW_V1_EDGES_RIGHT,
};
