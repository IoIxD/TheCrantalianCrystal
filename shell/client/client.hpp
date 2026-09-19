#pragma once
#include <cstdint>
#include <vector>
#include <wayland-client.h>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "../protocol/river-input-management-v1-protocol.h"
#include "../protocol/river-window-management-v1-protocol.h"
#include "../protocol/river-xkb-bindings-v1-protocol.h"

class TCCClient {
  enum Action {
    ACTION_NONE,
    ACTION_SPAWN_TERMINAL,
    ACTION_CLOSE,
    ACTION_FOCUS_NEXT,
    ACTION_MOVE,
    ACTION_RESIZE,
    ACTION_EXIT,
  };

  enum SeatOp {
    SEAT_OP_NONE,
    SEAT_OP_MOVE,
    SEAT_OP_RESIZE,
  };

  struct Seat;

  struct Window {
    TCCClient *client;
    river_window_v1 *id;
    river_node_v1 *node;

    bool is_new = false;
    bool closed = false;

    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;

    Seat *pointer_move_requested = nullptr;
    Seat *pointer_resize_requested = nullptr;
    uint32_t pointer_resize_requested_edges = 0;
  };

  struct Output {
    TCCClient *client;
    river_output_v1 *id;
    bool removed = false;
  };

  struct XkbBinding {
    TCCClient *client;
    river_xkb_binding_v1 *id;
    Seat *seat;
    Action action;
  };

  struct PointerBinding {
    TCCClient *client;
    river_pointer_binding_v1 *id;
    Seat *seat;
    Action action;
  };

  struct Seat {
    TCCClient *client;
    river_seat_v1 *id;
    bool is_new = false;
    bool removed = false;

    Window *focused = nullptr;
    Window *hovered = nullptr;
    Window *interacted = nullptr;

    std::vector<XkbBinding *> xkb_bindings;
    std::vector<PointerBinding *> pointer_bindings;
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
  };

  wl_display *mDisplay = nullptr;
  wl_registry *mRegistry = nullptr;
  wl_compositor *mCompositor = nullptr;

  std::vector<Window *> mWindows;
  std::vector<Output *> mOutputs;
  std::vector<Seat *> mSeats;

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
  static void river_window_dimensions_hint(void *data,
                                           struct river_window_v1 *id,
                                           int32_t min_width,
                                           int32_t min_height,
                                           int32_t max_width,
                                           int32_t max_height);
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
  static void
  river_window_pointer_move_requested(void *data, struct river_window_v1 *id,
                                      struct river_seat_v1 *seat);
  static void
  river_window_pointer_resize_requested(void *data,
                                        struct river_window_v1 *id,
                                        struct river_seat_v1 *seat,
                                        uint32_t edges);
  static void
  river_window_show_window_menu_requested(void *data,
                                          struct river_window_v1 *id,
                                          int32_t x, int32_t y);
  static void river_window_maximize_requested(void *data,
                                              struct river_window_v1 *id);
  static void river_window_unmaximize_requested(void *data,
                                                struct river_window_v1 *id);
  static void
  river_window_fullscreen_requested(void *data, struct river_window_v1 *id,
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
  static void
  river_seat_pointer_enter(void *data, struct river_seat_v1 *id,
                           struct river_window_v1 *window);
  static void river_seat_pointer_leave(void *data, struct river_seat_v1 *id);
  static void
  river_seat_window_interaction(void *data, struct river_seat_v1 *id,
                                struct river_window_v1 *window);
  static void
  river_seat_shell_surface_interaction(
      void *data, struct river_seat_v1 *id,
      struct river_shell_surface_v1 *shell_surface);
  static void river_seat_op_delta(void *data, struct river_seat_v1 *id,
                                  int32_t dx, int32_t dy);
  static void river_seat_op_release(void *data, struct river_seat_v1 *id);
  static void river_seat_pointer_position(void *data,
                                          struct river_seat_v1 *id,
                                          int32_t x, int32_t y);

  static void
  river_xkb_binding_pressed(void *data,
                            struct river_xkb_binding_v1 *river_xkb_binding_v1);
  static void river_xkb_binding_released(
      void *data, struct river_xkb_binding_v1 *river_xkb_binding_v1);
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
  void xkb_binding_destroy(XkbBinding *binding);

  void pointer_binding_create(Seat *seat, uint32_t mods, uint32_t button,
                              Action action);
  void pointer_binding_destroy(PointerBinding *binding);

  void seat_maybe_destroy(Seat *seat);
  void seat_focus(Seat *seat, Window *window);
  void seat_pointer_move(Seat *seat, Window *window);
  void seat_pointer_resize(Seat *seat, Window *window, uint32_t edges);
  void seat_action(Seat *seat, Action action);
  void seat_manage(Seat *seat);
  void seat_render(Seat *seat);

public:
  TCCClient();
  void run();
};
