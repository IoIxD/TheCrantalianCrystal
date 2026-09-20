#include "client.hpp"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <vector>

#include <linux/input-event-codes.h>

void TCCClient::river_wm_unavailable(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  fprintf(stderr, "error: another window manager is already running\n");
  exit(1);
}

void TCCClient::river_wm_finished(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  exit(0);
}

void TCCClient::river_wm_manage_start(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  TCCClient *client = (TCCClient *)data;

  // Destroy closed windows and removed outputs/seats.
  for (Output *output : std::vector<Output *>(client->mOutputs)) {
    client->output_maybe_destroy(output);
  }
  for (Window *window : std::vector<Window *>(client->mWindows)) {
    client->window_maybe_destroy(window);
  }
  for (Seat *seat : std::vector<Seat *>(client->mSeats)) {
    client->seat_maybe_destroy(seat);
  }

  // Carry out window management policy
  for (Window *window : client->mWindows) {
    client->window_manage(window);
  }
  for (Seat *seat : client->mSeats) {
    client->seat_manage(seat);
  }

  river_window_manager_v1_manage_finish(client->mRiverWindowManager);
}

void TCCClient::river_wm_render_start(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  TCCClient *client = (TCCClient *)data;

  for (Window *window : client->mWindows) {
    if (window->has_decor) {
      window->egl_draw();
      river_decoration_v1_sync_next_commit(window->decor);
      wl_surface_commit(window->decor_surface);
    }
  }

  for (Seat *seat : client->mSeats) {
    client->seat_render(seat);
  }

  river_window_manager_v1_render_finish(client->mRiverWindowManager);
}

// Ignored events
void TCCClient::river_wm_session_locked(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {}
void TCCClient::river_wm_session_unlocked(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {}

void TCCClient::river_wm_window(
    void *data, struct river_window_manager_v1 *river_window_manager_v1,
    struct river_window_v1 *id) {
  TCCClient *client = (TCCClient *)data;

  Window *window = new Window();
  window->client = client;
  window->id = id;
  window->node = river_window_v1_get_node(window->id);
  window->is_new = true;

  river_window_v1_add_listener(window->id, &client->mRiverWindowListener,
                               window);

  client->mWindows.push_back(window);
}

void TCCClient::river_wm_output(
    void *data, struct river_window_manager_v1 *river_window_manager_v1,
    struct river_output_v1 *id) {
  TCCClient *client = (TCCClient *)data;

  Output *output = new Output();
  output->client = client;
  output->id = id;

  river_output_v1_add_listener(output->id, &client->mRiverOutputListener,
                               output);

  client->mOutputs.push_back(output);
}

void TCCClient::river_wm_seat(
    void *data, struct river_window_manager_v1 *river_window_manager_v1,
    struct river_seat_v1 *id) {
  TCCClient *client = (TCCClient *)data;

  Seat *seat = new Seat();
  seat->client = client;
  seat->id = id;
  seat->is_new = true;

  river_seat_v1_add_listener(seat->id, &client->mRiverSeatListener, seat);

  client->mSeats.push_back(seat);
}

void TCCClient::river_window_closed(void *data, struct river_window_v1 *id) {
  Window *window = (Window *)data;
  window->closed = true;
}

void TCCClient::river_window_dimensions(void *data, struct river_window_v1 *id,
                                        int32_t width, int32_t height) {
  Window *window = (Window *)data;
  window->width = width;
  window->height = height;
  window->decor_width = window->width + 6;
  window->decor_height = window->height + 27;
}

void TCCClient::river_window_pointer_move_requested(
    void *data, struct river_window_v1 *id, struct river_seat_v1 *seat) {
  Window *window = (Window *)data;
  window->pointer_move_requested = (Seat *)river_seat_v1_get_user_data(seat);
}

void TCCClient::river_window_pointer_resize_requested(
    void *data, struct river_window_v1 *id, struct river_seat_v1 *seat,
    uint32_t edges) {
  Window *window = (Window *)data;
  window->pointer_resize_requested = (Seat *)river_seat_v1_get_user_data(seat);
  window->pointer_resize_requested_edges = edges;
}
void TCCClient::river_window_decoration_hint(void *data,
                                             struct river_window_v1 *id,
                                             uint32_t hint) {
  Window *window = (Window *)data;

  bool wants_decor = hint != RIVER_WINDOW_V1_DECORATION_HINT_ONLY_SUPPORTS_CSD;

  if (wants_decor == window->has_decor) {
    return;
  }

  if (wants_decor) {
    window->has_decor = true;
    window->decor_surface =
        wl_compositor_create_surface(window->client->mCompositor);
    window->decor =
        river_window_v1_get_decoration_below(id, window->decor_surface);
    window->decor_width = window->width + 6;
    window->decor_height = window->height + 27;
    window->setup_egl();
  }
}
// Ignored events
void TCCClient::river_window_dimensions_hint(
    void *data, struct river_window_v1 *id, int32_t min_width,
    int32_t min_height, int32_t max_width, int32_t max_height) {}
void TCCClient::river_window_app_id(void *data, struct river_window_v1 *id,
                                    const char *app_id) {}
void TCCClient::river_window_title(void *data, struct river_window_v1 *id,
                                   const char *title) {}
void TCCClient::river_window_parent(void *data, struct river_window_v1 *id,
                                    struct river_window_v1 *parent) {}

void TCCClient::river_window_show_window_menu_requested(
    void *data, struct river_window_v1 *id, int32_t x, int32_t y) {}
void TCCClient::river_window_maximize_requested(void *data,
                                                struct river_window_v1 *id) {}
void TCCClient::river_window_unmaximize_requested(void *data,
                                                  struct river_window_v1 *id) {}
void TCCClient::river_window_fullscreen_requested(
    void *data, struct river_window_v1 *id, struct river_output_v1 *output) {}
void TCCClient::river_window_exit_fullscreen_requested(
    void *data, struct river_window_v1 *id) {}
void TCCClient::river_window_minimize_requested(void *data,
                                                struct river_window_v1 *id) {}
void TCCClient::river_window_unreliable_pid(void *data,
                                            struct river_window_v1 *id,
                                            int32_t unreliable_pid) {}
void TCCClient::river_window_presentation_hint(void *data,
                                               struct river_window_v1 *id,
                                               uint32_t hint) {}
void TCCClient::river_window_identifier(void *data, struct river_window_v1 *id,
                                        const char *identifier) {}

void TCCClient::river_output_removed(void *data, struct river_output_v1 *id) {
  Output *output = (Output *)data;
  output->removed = true;
}

// Ignored events
void TCCClient::river_output_wl_output(void *data, struct river_output_v1 *id,
                                       uint32_t name) {}
void TCCClient::river_output_position(void *data, struct river_output_v1 *id,
                                      int32_t x, int32_t y) {}
void TCCClient::river_output_dimensions(void *data, struct river_output_v1 *id,
                                        int32_t width, int32_t height) {}

void TCCClient::river_seat_removed(void *data, struct river_seat_v1 *id) {
  Seat *seat = (Seat *)data;
  seat->removed = true;
}

void TCCClient::river_seat_pointer_enter(void *data, struct river_seat_v1 *id,
                                         struct river_window_v1 *window) {
  Seat *seat = (Seat *)data;
  seat->hovered = (Window *)river_window_v1_get_user_data(window);
}

void TCCClient::river_seat_pointer_leave(void *data, struct river_seat_v1 *id) {
  Seat *seat = (Seat *)data;
  seat->hovered = nullptr;
}

void TCCClient::river_seat_window_interaction(void *data,
                                              struct river_seat_v1 *id,
                                              struct river_window_v1 *window) {
  Seat *seat = (Seat *)data;
  seat->interacted = (Window *)river_window_v1_get_user_data(window);
}

void TCCClient::river_seat_op_delta(void *data, struct river_seat_v1 *id,
                                    int32_t dx, int32_t dy) {
  Seat *seat = (Seat *)data;
  seat->op_dx = dx;
  seat->op_dy = dy;
}

void TCCClient::river_seat_op_release(void *data, struct river_seat_v1 *id) {
  Seat *seat = (Seat *)data;
  seat->op_release = true;
}

// Ignored events
void TCCClient::river_seat_wl_seat(void *data, struct river_seat_v1 *id,
                                   uint32_t seat_id) {}
void TCCClient::river_seat_shell_surface_interaction(
    void *data, struct river_seat_v1 *id,
    struct river_shell_surface_v1 *shell_surface) {}
void TCCClient::river_seat_pointer_position(void *data,
                                            struct river_seat_v1 *id, int32_t x,
                                            int32_t y) {}

void TCCClient::output_maybe_destroy(Output *output) {
  if (!output->removed) {
    return;
  }
  river_output_v1_destroy(output->id);
  std::erase(mOutputs, output);
  delete output;
}

void TCCClient::window_maybe_destroy(Window *window) {
  if (!window->closed) {
    return;
  }

  for (Seat *seat : mSeats) {
    if (seat->focused == window) {
      seat->focused = nullptr;
    }
    if (seat->op_window == window) {
      river_seat_v1_op_end(seat->id);
      seat->op = SEAT_OP_NONE;
      seat->op_window = nullptr;
    }
  }

  river_window_v1_destroy(window->id);
  std::erase(mWindows, window);
  delete window;
}

void TCCClient::window_set_position(Window *window, int32_t x, int32_t y) {
  river_node_v1_set_position(window->node, x, y);
  window->x = x;
  window->y = y;
}

void TCCClient::window_manage(Window *window) {
  if (window->is_new) {
    window->is_new = false;
    river_window_v1_use_ssd(window->id);
    if (window->has_decor) {
      river_decoration_v1_set_offset(window->decor, -3, -23);
      window_set_position(window, 3, 23);

      window->setup_egl();
      window->egl_draw();
    } else {
      window_set_position(window, 0, 0);
    }
    river_window_v1_propose_dimensions(window->id, 0, 0);
  }
  if (window->pointer_move_requested != nullptr) {
    seat_pointer_move(window->pointer_move_requested, window);
    window->pointer_move_requested = nullptr;
  }
  if (window->pointer_resize_requested != nullptr) {
    seat_pointer_resize(window->pointer_resize_requested, window,
                        window->pointer_resize_requested_edges);
    window->pointer_resize_requested = nullptr;
  }
}

void TCCClient::seat_maybe_destroy(Seat *seat) {
  if (!seat->removed) {
    return;
  }

  for (XkbBinding *binding : std::vector<XkbBinding *>(seat->xkb_bindings)) {
    xkb_binding_destroy(binding);
  }
  for (PointerBinding *binding :
       std::vector<PointerBinding *>(seat->pointer_bindings)) {
    pointer_binding_destroy(binding);
  }

  river_seat_v1_destroy(seat->id);
  std::erase(mSeats, seat);
  delete seat;
}

void TCCClient::seat_focus(Seat *seat, Window *window) {
  // Focus the top window (if any) when there is no explicit target.
  if (window == nullptr && !mWindows.empty()) {
    window = mWindows.back();
  }

  if (seat->focused == window) {
    return;
  }

  if (window != nullptr) {
    river_seat_v1_focus_window(seat->id, window->id);
    river_node_v1_place_top(window->node);
    std::erase(mWindows, window);
    mWindows.push_back(window);
  } else {
    river_seat_v1_clear_focus(seat->id);
  }

  seat->focused = window;
}

void TCCClient::seat_pointer_move(Seat *seat, Window *window) {
  seat_focus(seat, window);
  river_seat_v1_op_start_pointer(seat->id);
  seat->op = SEAT_OP_MOVE;
  seat->op_window = window;
  seat->op_start_x = window->x;
  seat->op_start_y = window->y;
  seat->op_dx = 0;
  seat->op_dy = 0;
}

void TCCClient::seat_pointer_resize(Seat *seat, Window *window,
                                    uint32_t edges) {
  seat_focus(seat, window);
  river_window_v1_inform_resize_start(window->id);
  river_seat_v1_op_start_pointer(seat->id);
  seat->op = SEAT_OP_RESIZE;
  seat->op_window = window;
  seat->op_edges = edges;
  seat->op_start_x = window->x;
  seat->op_start_y = window->y;
  seat->op_start_width = window->width;
  seat->op_start_height = window->height;
  seat->op_dx = 0;
  seat->op_dy = 0;
}

void TCCClient::seat_action(Seat *seat, Action action) {
  switch (action) {
  case ACTION_NONE:
    break;
  case ACTION_SPAWN_TERMINAL:
    if (fork() == 0) {
      execlp("konsole", "konsole", (char *)nullptr);
      _exit(1);
    }
    break;
  case ACTION_CLOSE:
    if (seat->focused != nullptr) {
      river_window_v1_close(seat->focused->id);
    }
    break;
  case ACTION_FOCUS_NEXT:
    if (!mWindows.empty()) {
      // Focus the bottom window
      seat_focus(seat, mWindows.front());
    }
    break;
  case ACTION_MOVE:
    if (seat->op == SEAT_OP_NONE && seat->hovered != nullptr) {
      seat_pointer_move(seat, seat->hovered);
    }
    break;
  case ACTION_RESIZE:
    if (seat->op == SEAT_OP_NONE && seat->hovered != nullptr) {
      seat_pointer_resize(seat, seat->hovered,
                          RIVER_WINDOW_V1_EDGES_BOTTOM |
                              RIVER_WINDOW_V1_EDGES_RIGHT);
    }
    break;
  case ACTION_EXIT:
    river_window_manager_v1_exit_session(mRiverWindowManager);
    break;
  }
}

void TCCClient::seat_manage(Seat *seat) {
  if (seat->is_new) {
    seat->is_new = false;
    const uint32_t super = RIVER_SEAT_V1_MODIFIERS_MOD4;
    xkb_binding_create(seat, super, XKB_KEY_space, ACTION_SPAWN_TERMINAL);
    xkb_binding_create(seat, super, XKB_KEY_q, ACTION_CLOSE);
    xkb_binding_create(seat, super, XKB_KEY_n, ACTION_FOCUS_NEXT);
    xkb_binding_create(seat, super, XKB_KEY_Escape, ACTION_EXIT);
    pointer_binding_create(seat, super, BTN_LEFT, ACTION_MOVE);
    pointer_binding_create(seat, super, BTN_RIGHT, ACTION_RESIZE);
  }

  // If no window was interacted with in the current manage sequence,
  // intentionally pass nullptr to ensure the window on top has focus.
  // This is necessary to handle new windows for example.
  seat_focus(seat, seat->interacted);
  seat->interacted = nullptr;

  seat_action(seat, seat->pending_action);
  seat->pending_action = ACTION_NONE;

  switch (seat->op) {
  case SEAT_OP_NONE:
    break;
  case SEAT_OP_MOVE:
    if (seat->op_release) {
      river_seat_v1_op_end(seat->id);
      seat->op = SEAT_OP_NONE;
      seat->op_window = nullptr;
    }
    break;
  case SEAT_OP_RESIZE: {
    if (seat->op_release) {
      river_window_v1_inform_resize_end(seat->op_window->id);
      river_seat_v1_op_end(seat->id);
      seat->op = SEAT_OP_NONE;
      seat->op_window = nullptr;
      break;
    }
    int32_t width = seat->op_start_width;
    int32_t height = seat->op_start_height;
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_LEFT) != 0) {
      width -= seat->op_dx;
    }
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_RIGHT) != 0) {
      width += seat->op_dx;
    }
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_TOP) != 0) {
      height -= seat->op_dy;
    }
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_BOTTOM) != 0) {
      height += seat->op_dy;
    }
    river_window_v1_propose_dimensions(
        seat->op_window->id, width > 1 ? width : 1, height > 1 ? height : 1);
    break;
  }
  }
  seat->op_release = false;
}

void TCCClient::seat_render(Seat *seat) {

  switch (seat->op) {
  case SEAT_OP_NONE:

    break;
  case SEAT_OP_MOVE:
    window_set_position(seat->op_window, seat->op_start_x + seat->op_dx,
                        seat->op_start_y + seat->op_dy);
    break;
  case SEAT_OP_RESIZE: {
    int32_t x = seat->op_start_x;
    int32_t y = seat->op_start_y;
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_LEFT) != 0) {
      x += seat->op_start_width - seat->op_window->width;
    }
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_TOP) != 0) {
      y += seat->op_start_height - seat->op_window->height;
    }
    window_set_position(seat->op_window, x, y);
    break;
  }
  }
}
