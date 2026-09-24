#include "client.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include <linux/input-event-codes.h>

#include "../desktop/desktop.hpp"

void TCCClient::river_wm_unavailable(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  fprintf(stderr, "error: another window manager is already running\n");
  exit(1);
}

void TCCClient::river_wm_finished(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  fprintf(stderr, "wm finished\n");
  exit(0);
}

void TCCClient::river_wm_manage_start(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  TCCClient *client = (TCCClient *)data;

  // Destroy closed windows and removed outputs/seats.
  // also update any of the outputs' desktop clients
  for (Output *output : client->mOutputs) {
    output->desktop_client->step();

    client->output_maybe_destroy(output);
  }
  for (Output *output : client->mOutputs) {
    for (Window *window : output->windows) {
      client->window_maybe_destroy(window);
    }
  }
  for (Seat *seat : client->mSeats) {
    client->seat_maybe_destroy(seat);
  }

  // Carry out window management policy
  for (Output *output : client->mOutputs) {
    for (Window *window : output->windows) {
      client->window_manage(window);
    }
  }
  for (Seat *seat : client->mSeats) {
    client->seat_manage(seat);
  }

  river_window_manager_v1_manage_finish(client->mRiverWindowManager);
}

void TCCClient::river_wm_render_start(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  TCCClient *client = (TCCClient *)data;

  for (Output *output : client->mOutputs) {
    for (Window *window : output->windows) {
      if (window->has_decor) {
        window->decor_draw();
        client->window_position_nav_surfaces(window);
        river_decoration_v1_sync_next_commit(window->decor_decor);
        wl_surface_commit(window->decor_surface);
      }
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
  window->client = client->shared_from_this();
  window->id = id;
  window->node = river_window_v1_get_node(window->id);
  window->is_new = true;

  river_window_v1_add_listener(window->id, &client->mRiverWindowListener,
                               window);

  /* todo: whatever output the cursor is on. */
  client->mOutputs[0]->windows.push_back(window);
}

void TCCClient::river_wm_output(
    void *data, struct river_window_manager_v1 *river_window_manager_v1,
    struct river_output_v1 *id) {
  TCCClient *client = (TCCClient *)data;

  auto output = new Output();
  output->client = client->shared_from_this();
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
  seat->client = client->shared_from_this();
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
  window->decor_width = window->width + SSD_BORDER_SIZE + SSD_BORDER_SIZE;
  window->decor_height = window->height + SSD_BORDER_SIZE_TOTAL;
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
    window->decor_decor =
        river_window_v1_get_decoration_below(id, window->decor_surface);

    window->client->window_create_nav_surfaces(window);

    window->decor_width = window->width + SSD_BORDER_SIZE + SSD_BORDER_SIZE;
    window->decor_height = window->height + SSD_BORDER_SIZE_TOTAL;
    window->setup_decor();
  }
}

void TCCClient::river_window_title(void *data, struct river_window_v1 *id,
                                   const char *title) {
  Window *window = (Window *)data;

  strncpy(window->title, title, sizeof(window->title) - 1);
}
void TCCClient::river_window_app_id(void *data, struct river_window_v1 *id,
                                    const char *app_id) {
  Window *window = (Window *)data;
  if (app_id) {
    strncpy(window->app_id, app_id, sizeof(window->app_id) - 1);
  } else {
    window->app_id[0] = '\0';
  }
}
// Ignored events
void TCCClient::river_window_dimensions_hint(
    void *data, struct river_window_v1 *id, int32_t min_width,
    int32_t min_height, int32_t max_width, int32_t max_height) {}

void TCCClient::river_window_parent(void *data, struct river_window_v1 *id,
                                    struct river_window_v1 *parent) {}

void TCCClient::river_window_show_window_menu_requested(
    void *data, struct river_window_v1 *id, int32_t x, int32_t y) {}
void TCCClient::river_window_maximize_requested(void *data,
                                                struct river_window_v1 *id) {
  Window *window = (Window *)data;
  window->client->window_maximize(window);
}
void TCCClient::river_window_unmaximize_requested(void *data,
                                                  struct river_window_v1 *id) {
  Window *window = (Window *)data;
  window->client->window_maximize(window);
}
void TCCClient::river_window_fullscreen_requested(
    void *data, struct river_window_v1 *id, struct river_output_v1 *output) {}
void TCCClient::river_window_exit_fullscreen_requested(
    void *data, struct river_window_v1 *id) {}
void TCCClient::river_window_minimize_requested(void *data,
                                                struct river_window_v1 *id) {
  Window *window = (Window *)data;
  window->client->window_minimize(window);
}
void TCCClient::river_window_unreliable_pid(void *data,
                                            struct river_window_v1 *id,
                                            int32_t unreliable_pid) {}
void TCCClient::river_window_presentation_hint(void *data,
                                               struct river_window_v1 *id,
                                               uint32_t hint) {}
void TCCClient::river_window_identifier(void *data, struct river_window_v1 *id,
                                        const char *identifier) {}

void TCCClient::river_output_removed(void *data, struct river_output_v1 *id) {
  TCCClient::Output *output = (TCCClient::Output *)data;
  output->removed = true;
}

void TCCClient::river_output_wl_output(void *data, struct river_output_v1 *id,
                                       uint32_t name) {
  TCCClient::Output *output = (TCCClient::Output *)data;
  output->width = 10;
  output->height = 10;
  output->desktop_client = std::make_unique<TCCDesktopClient>(output);
}
void TCCClient::river_output_position(void *data, struct river_output_v1 *id,
                                      int32_t x, int32_t y) {
  TCCClient::Output *output = (TCCClient::Output *)data;
  output->x = x;
  output->y = y;
}
void TCCClient::river_output_dimensions(void *data, struct river_output_v1 *id,
                                        int32_t width, int32_t height) {
  TCCClient::Output *output = (TCCClient::Output *)data;
  output->width = (width < 1) ? 1 : width;
  output->height = (height < 1) ? 1 : height;
}

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

  if (seat->interacted->maximized) {
    return;
  }

  uint32_t edges = seat->client->get_pointer_edges(seat, seat->interacted);

  if (edges) {
    seat->interacted->pointer_resize_requested = seat;
    seat->interacted->pointer_resize_requested_edges = edges;
  } else {
    int y = seat->pointer_y - seat->interacted->y;
    // Clicking a nav button shouldn't start dragging the window.
    if (y < 0 && seat->pointer_nav_button == NAV_BUTTON_NONE) {
      seat->interacted->pointer_move_requested = seat;
    }
  }
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

void TCCClient::river_seat_pointer_position(void *data,
                                            struct river_seat_v1 *id, int32_t x,
                                            int32_t y) {
  Seat *seat = (Seat *)data;
  seat->pointer_x = x;
  seat->pointer_y = y;
}

void TCCClient::river_seat_wl_seat(void *data, struct river_seat_v1 *id,
                                   uint32_t seat_id) {
  Seat *seat = (Seat *)data;
  TCCClient *client = seat->client.get();

  auto it = client->mWlSeatVersions.find(seat_id);
  if (it == client->mWlSeatVersions.end()) {
    fprintf(stderr, "river_seat_v1.wl_seat: unknown wl_seat global %u\n",
            seat_id);
    return;
  }

  // Our wl_pointer_listener only implements events up to axis_value120.
  uint32_t version =
      std::min<uint32_t>(it->second, WL_POINTER_AXIS_VALUE120_SINCE_VERSION);
  seat->wl_seat_id = (wl_seat *)wl_registry_bind(client->mRegistry, seat_id,
                                                 &wl_seat_interface, version);
  wl_seat_add_listener(seat->wl_seat_id, &client->mWlSeatListener, seat);
}
void TCCClient::river_seat_shell_surface_interaction(
    void *data, struct river_seat_v1 *id,
    struct river_shell_surface_v1 *shell_surface) {}

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
    if (seat->pointer_window == window) {
      seat->pointer_window = nullptr;
      seat->pointer_nav_button = NAV_BUTTON_NONE;
      seat->nav_button_pressed = NAV_BUTTON_NONE;
    }
    if (seat->op_window == window) {
      river_seat_v1_op_end(seat->id);
      seat->op = SEAT_OP_NONE;
      seat->op_window = nullptr;
    }
  }

  window_destroy_nav_surfaces(window);
  river_window_v1_destroy(window->id);

  for (auto out : mOutputs) {
    for (auto win : out->windows) {
      if (!out->windows.empty()) {
        if (win == window) {
          std::erase(out->windows, window);
          break;
        };
      }
    }
  }
  delete window;
}

void TCCClient::window_set_position(Window *window, int32_t x, int32_t y) {
  river_node_v1_set_position(window->node, x, y);
  window->x = x;
  window->y = y;
}

void TCCClient::window_maximize(Window *window) {
  for (auto out : mOutputs) {
    for (auto win : out->windows) {
      if (win == window) {
        win->maximized = !win->maximized;
        if (win->maximized) {
          win->saved_x = win->x;
          win->saved_y = win->y;
          win->saved_width = win->width;
          win->saved_height = win->height;
          if (win->has_decor) {
            window_set_position(window, SSD_BORDER_SIZE, SSD_BORDER_SIZE_TOP);
            river_window_v1_propose_dimensions(
                win->id, out->width - (SSD_BORDER_SIZE * 2),
                out->height - SSD_BORDER_SIZE_TOP - SSD_BORDER_SIZE);
          } else {
            window_set_position(window, 0, 0);
            river_window_v1_propose_dimensions(win->id, out->width,
                                               out->height);
          }
          river_window_v1_inform_maximized(win->id);
        } else {
          window_set_position(win, win->saved_x, win->saved_y);
          river_window_v1_propose_dimensions(win->id, win->saved_width,
                                             win->saved_height);
          river_window_v1_inform_unmaximized(win->id);
        }
        break;
      };
    }
  }
}

void TCCClient::window_minimize(Window *window) {
  for (auto out : mOutputs) {
    for (auto win : out->windows) {
      if (win == window) {
        win->minimized = !win->minimized;
        if (win->minimized) {
          std::erase(out->windows, window);
          out->minimized_windows.push_back(window);
          river_window_v1_hide(window->id);
        } else {
          std::erase(out->minimized_windows, window);
          out->windows.push_back(window);
          river_window_v1_show(window->id);
        }
        break;
      }
    }
  }
}

void TCCClient::window_manage(Window *window) {
  if (window->is_new) {
    window->is_new = false;
    river_window_v1_use_ssd(window->id);
    if (window->has_decor) {
      river_decoration_v1_set_offset(window->decor_decor, -SSD_BORDER_SIZE,
                                     -SSD_BORDER_SIZE_TOP);

      window->center_requested = true;
      river_window_v1_hide(window->id); /* hide the window so that we don't see
                                           it in its initial position */

      window->setup_decor();
      window->decor_draw();
    } else {
      window_set_position(window, 0, 0);
    }
    river_window_v1_propose_dimensions(window->id, 0, 0);
  }
  switch (window->pending_nav_action) {
  case NAV_BUTTON_CLOSE:
    river_window_v1_close(window->id);
    break;
  case NAV_BUTTON_MAX:
    window_maximize(window);
    break;
  case NAV_BUTTON_MIN:
    window_minimize(window);
    break;
  default:
    break;
  }
  window->pending_nav_action = NAV_BUTTON_NONE;

  if (window->pointer_move_requested != nullptr) {
    if (window->maximized) {
      window_maximize(window);
    }
    seat_pointer_move(window->pointer_move_requested, window);
    window->pointer_move_requested = nullptr;
  }
  if (window->pointer_resize_requested != nullptr) {
    if (!window->maximized) {
      seat_pointer_resize(window->pointer_resize_requested, window,
                          window->pointer_resize_requested_edges);
    }
    window->pointer_resize_requested = nullptr;
  }

  if (window->center_requested && window->width > 0 && window->height > 0) {
    for (auto out : mOutputs) {
      for (auto win : out->windows) {
        if (win == window) {
          window_set_position(
              window, ((out->width / 2) - (win->width / 2)) + SSD_BORDER_SIZE,
              ((out->height / 2) - (win->height / 2)) + SSD_BORDER_SIZE_TOP);
          break;
        }
      }
    }
    window->center_requested = false;
    river_window_v1_show(window->id);

    window->decor_draw();
  }
}

void TCCClient::seat_maybe_destroy(Seat *seat) {
  if (!seat->removed) {
    return;
  }

  for (std::shared_ptr<XkbBinding> binding : seat->xkb_bindings) {
    xkb_binding_destroy(binding);
  }
  for (std::shared_ptr<PointerBinding> binding : seat->pointer_bindings) {
    pointer_binding_destroy(binding);
  }

  if (seat->cursor_shape_device) {
    wp_cursor_shape_device_v1_destroy(seat->cursor_shape_device);
  }
  if (seat->wl_pointer_id) {
    wl_pointer_release(seat->wl_pointer_id);
  }
  if (seat->wl_seat_id) {
    wl_seat_release(seat->wl_seat_id);
  }

  river_seat_v1_destroy(seat->id);
  std::erase(mSeats, seat);
  delete seat;
}

void TCCClient::seat_focus(Seat *seat, Window *window) {
  // Focus the top window (if any) when there is no explicit target.
  if (!window) {
    for (auto out : mOutputs) {
      for (auto win : out->windows) {
        if (win == window) {
          if (!out->windows.empty()) {
            window = out->windows.back();
            break;
          };
        }
      }
    }
  }

  if (seat->focused == window) {
    return;
  }

  if (window != nullptr) {
    river_seat_v1_focus_window(seat->id, window->id);
    river_node_v1_place_top(window->node);
    for (auto out : mOutputs) {
      for (auto win : out->windows) {
        if (win == window) {
          if (!out->windows.empty()) {
            std::erase(out->windows, window);
            out->windows.push_back(window);
            break;
          };
        }
      }
    }
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
    /* todo: whatever output the cursor is on. */
    if (!mOutputs[0]->windows.empty()) {
      // Focus the bottom window
      seat_focus(seat, mOutputs[0]->windows.front());
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
        seat->op_window->id,
        (width > SSD_BORDER_SIZE_TOTAL) ? width : SSD_BORDER_SIZE_TOTAL,
        (height > SSD_BORDER_SIZE_TOTAL) ? height : SSD_BORDER_SIZE_TOTAL);
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

uint32_t TCCClient::get_pointer_edges(Seat *seat, Window *window, int x,
                                      int y) {
  uint32_t edges = 0;

  if (x == -1)
    x = seat->pointer_x - window->x;
  if (y == -1)
    y = seat->pointer_y - window->y;

#define Y_LIMIT -(SSD_BORDER_SIZE_TOP - SSD_BORDER_SIZE)

  auto x_check = x > window->width;
  auto y_check = y > window->height;
  if (x_check || y_check) {
    if (x > window->width - SSD_BORDER_LEEWAY)
      edges |= RIVER_WINDOW_V1_EDGES_RIGHT;
    if (y > window->height - SSD_BORDER_LEEWAY)
      edges |= RIVER_WINDOW_V1_EDGES_BOTTOM;
  }
  return edges;
};

void TCCClient::nav_button_action(uint8_t action, bool released,
                                  Window *window) {
  /* close all holds  */
  window->close_held = window->maximize_held = window->minimize_held = false;

  switch (action) {
  case TCCClient::NAV_BUTTON_CLOSE:
    window->close_held = !released;
    break;
  case TCCClient::NAV_BUTTON_MAX:
    window->maximize_held = !released;
    break;
  case TCCClient::NAV_BUTTON_MIN:
    window->minimize_held = !released;
    break;
  default:
    break;
  }

  // The actual action has to happen in a manage sequence, see window_manage.
  if (released) {
    window->pending_nav_action = action;
  }
  // Kick off a manage + render sequence so the held state gets redrawn.
  river_window_manager_v1_manage_dirty(mRiverWindowManager);
};

void TCCClient::nav_button_hover(uint8_t button, Window *window) {
  window->close_hover = button == TCCClient::NAV_BUTTON_CLOSE;
  window->maximize_hover = button == TCCClient::NAV_BUTTON_MAX;
  window->minimize_hover = button == TCCClient::NAV_BUTTON_MIN;

  // Kick off a manage + render sequence so the hover state gets redrawn.
  river_window_manager_v1_manage_dirty(mRiverWindowManager);
}

uint8_t TCCClient::get_pressed_nav_button(Seat *seat, Window *window) {
  // Pointer position in decoration surface coordinates.
  int x = seat->pointer_x - window->x + SSD_BORDER_SIZE;
  int y = seat->pointer_y - window->y + SSD_BORDER_SIZE_TOP;
  if (y < SSD_NAV_BUTTON_Y || y >= SSD_NAV_BUTTON_Y + SSD_NAV_BUTTON_HEIGHT) {
    return NAV_BUTTON_NONE;
  }

  for (int i = NAV_BUTTON_NONE + 1; i < NAV_BUTTON_COUNT; i++) {
    int lo = window->decor_width - nav_button_x_from_right[i];
    if (x >= lo && x < lo + SSD_NAV_BUTTON_WIDTH) {
      return i;
    }
  }
  return NAV_BUTTON_NONE;
}

wl_buffer *TCCClient::get_nav_button_buffer() {
  if (mNavButtonBuffer) {
    return mNavButtonBuffer;
  }

  const int stride = SSD_NAV_BUTTON_WIDTH * 4;
  const int size = stride * SSD_NAV_BUTTON_HEIGHT;

  // ftruncate zero-fills, which is exactly the fully transparent buffer we
  // want, so there's no need to mmap it.
  int fd = memfd_create("tcc-nav-button", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, size) < 0) {
    perror("nav button buffer");
    exit(1);
  }

  wl_shm_pool *pool = wl_shm_create_pool(mShm, fd, size);
  mNavButtonBuffer = wl_shm_pool_create_buffer(pool, 0, SSD_NAV_BUTTON_WIDTH,
                                               SSD_NAV_BUTTON_HEIGHT, stride,
                                               WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);

  return mNavButtonBuffer;
}

void TCCClient::window_create_nav_surfaces(Window *window) {
  if (!mSubcompositor || !mShm) {
    fprintf(stderr, "wl_subcompositor or wl_shm not supported, nav buttons "
                    "won't be clickable\n");
    return;
  }

  for (int i = NAV_BUTTON_NONE + 1; i < NAV_BUTTON_COUNT; i++) {
    auto &nav = window->nav_surfaces[i];
    nav.surface = wl_compositor_create_surface(mCompositor);
    nav.subsurface = wl_subcompositor_get_subsurface(
        mSubcompositor, nav.surface, window->decor_surface);
    // Subsurfaces are synchronized by default, so this (and the position set
    // in window_position_nav_surfaces) lands with the decoration's commit.
    wl_surface_attach(nav.surface, get_nav_button_buffer(), 0, 0);
    wl_surface_commit(nav.surface);
  }
}

void TCCClient::window_destroy_nav_surfaces(Window *window) {
  for (auto &nav : window->nav_surfaces) {
    if (nav.subsurface) {
      wl_subsurface_destroy(nav.subsurface);
    }
    if (nav.surface) {
      wl_surface_destroy(nav.surface);
    }
    nav = {};
  }
}

void TCCClient::window_position_nav_surfaces(Window *window) {
  for (int i = NAV_BUTTON_NONE + 1; i < NAV_BUTTON_COUNT; i++) {
    if (window->nav_surfaces[i].subsurface) {
      wl_subsurface_set_position(
          window->nav_surfaces[i].subsurface,
          window->decor_width - nav_button_x_from_right[i], SSD_NAV_BUTTON_Y);
    }
  }
}
