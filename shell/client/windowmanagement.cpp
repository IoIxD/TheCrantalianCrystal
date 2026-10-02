#include "client.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "../desktop/desktop.hpp"
#include "../lock/lock.hpp"

void TCCClient::river_wm_unavailable(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  fprintf(stderr, "error: another window manager is already running\n");
  raise(SIGTRAP);
}

void TCCClient::river_wm_finished(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  fprintf(stderr, "wm finished\n");
  raise(SIGTRAP);
}

void TCCClient::river_wm_manage_start(
    void *data, struct river_window_manager_v1 *river_window_manager_v1) {
  TCCClient *client = (TCCClient *)data;

  // Destroy closed windows and removed outputs/seats.
  // also update any of the outputs' desktop clients
  for (Output *output : client->mOutputs) {
    if (output->desktop_client) {
      output->desktop_client->step();
    }

    client->output_maybe_destroy(output);
  }
  client->output_adopt_orphans();

  // Keep maximized/fullscreen windows filling their output if it moved or got
  // resized.
  for (Output *output : client->mOutputs) {
    if (!output->geometry_changed) {
      continue;
    }
    output->geometry_changed = false;
    for (Window *window : output->windows) {
      client->window_apply_geometry(window, output);
    }
    for (Window *window : output->minimized_windows) {
      client->window_apply_geometry(window, output);
    }
  }

  for (Output *output : client->mOutputs) {
    std::vector<Window *> windows;
    windows.insert(windows.end(), output->windows.begin(),
                   output->windows.end());
    windows.insert(windows.end(), output->minimized_windows.begin(),
                   output->minimized_windows.end());

    for (Window *window : windows) {
      if (window->queue_minimize) {
        client->window_minimize(window);
        window->queue_minimize = false;
        if (!window->minimized) {
          client->seat_focus(client->mSeats[0], window);
        }
        if (output->desktop_client) {
          output->desktop_client->step();
        }
      }
      client->window_maybe_destroy(window);
    }
  }
  for (Window *window : client->mOrphanedWindows) {
    client->window_maybe_destroy(window);
  }
  for (Seat *seat : client->mSeats) {
    client->seat_maybe_destroy(seat);
  }

  // Carry out window management policy
  // (iterating over a copy, minimizing takes windows out of the list)
  for (Output *output : client->mOutputs) {
    for (Window *window : std::vector<Window *>(output->windows)) {
      client->window_manage(window);
    }
  }
  for (Window *window : client->mOrphanedWindows) {
    client->window_manage(window);
  }
  for (Seat *seat : client->mSeats) {
    client->seat_manage(seat);
  }

  if (client->mDoInitialLaunch) {
    client->launch_initial_components();
    client->mDoInitialLaunch = false;
  }

  if (client->mMainOutputChanged) {
    river_layer_shell_output_v1_set_default(client->mMainOutput->layer_shell);
    client->mMainOutputChanged = false;
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
        client->window_position_resize_surfaces(window);
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

  // Output *output = client->output_under_pointer();
  Output *output = client->mMainOutput;
  if (output) {
    output->windows.push_back(window);
  } else {
    client->mOrphanedWindows.push_back(window);
  }
}

void TCCClient::river_wm_output(
    void *data, struct river_window_manager_v1 *river_window_manager_v1,
    struct river_output_v1 *id) {
  TCCClient *client = (TCCClient *)data;

  auto output = new Output();
  output->client = client->shared_from_this();
  output->id = id;
  output->layer_shell =
      river_layer_shell_v1_get_output(client->mRiverLayerShell, id);
  printf("%p\n", output->layer_shell);

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

  bool wants_decor =
      hint != RIVER_WINDOW_V1_DECORATION_HINT_ONLY_SUPPORTS_CSD &&
      hint != RIVER_WINDOW_V1_DECORATION_HINT_PREFERS_CSD;

  if (wants_decor == window->has_decor) {
    return;
  }

  if (wants_decor) {
    river_window_v1_use_ssd(id);
    window->has_decor = true;
    window->decor_surface =
        wl_compositor_create_surface(window->client->mCompositor);
    window->decor_decor =
        river_window_v1_get_decoration_below(id, window->decor_surface);

    window->client->window_create_nav_surfaces(window);
    window->client->window_create_resize_surfaces(window);

    window->decor_width = window->width + SSD_BORDER_SIZE + SSD_BORDER_SIZE;
    window->decor_height = window->height + SSD_BORDER_SIZE_TOTAL;
    window->setup_decor();
  }
}

void TCCClient::river_window_title(void *data, struct river_window_v1 *id,
                                   const char *title) {
  Window *window = (Window *)data;

  if (title) {
    strncpy(window->title, title, sizeof(window->title) - 1);
  }
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
void TCCClient::river_window_dimensions_hint(
    void *data, struct river_window_v1 *id, int32_t min_width,
    int32_t min_height, int32_t max_width, int32_t max_height) {
  Window *window = (Window *)data;

  window->min_width = min_width;
  window->min_height = min_height;
  window->max_width = max_width;
  window->max_height = max_height;

  if (min_width == max_width && min_height == max_height) {
    window->show_maximize = false;
  } else {
    window->show_maximize = true;
  }
}

// Ignored events

void TCCClient::river_window_parent(void *data, struct river_window_v1 *id,
                                    struct river_window_v1 *parent) {}

void TCCClient::river_window_show_window_menu_requested(
    void *data, struct river_window_v1 *id, int32_t x, int32_t y) {}
// These arrive before manage_start, so they only record what the window wants
// and window_manage carries it out.
void TCCClient::river_window_maximize_requested(void *data,
                                                struct river_window_v1 *id) {
  Window *window = (Window *)data;
  window->want_maximized = true;
}
void TCCClient::river_window_unmaximize_requested(void *data,
                                                  struct river_window_v1 *id) {
  Window *window = (Window *)data;
  window->want_maximized = false;
}
void TCCClient::river_window_fullscreen_requested(
    void *data, struct river_window_v1 *id, struct river_output_v1 *output) {
  Window *window = (Window *)data;
  window->want_fullscreen = true;
  window->want_fullscreen_output = output;
}
void TCCClient::river_window_exit_fullscreen_requested(
    void *data, struct river_window_v1 *id) {
  Window *window = (Window *)data;
  window->want_fullscreen = false;
}
void TCCClient::river_window_minimize_requested(void *data,
                                                struct river_window_v1 *id) {
  Window *window = (Window *)data;
  window->want_minimized = true;
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
  output->wl_output_name = name;
  output->desktop_client = std::make_unique<TCCDesktopClient>(output);
  output->client->mLock->output_added(output);
}
void TCCClient::river_output_position(void *data, struct river_output_v1 *id,
                                      int32_t x, int32_t y) {
  TCCClient::Output *output = (TCCClient::Output *)data;
  output->x = x;
  output->y = y;
  output->geometry_changed = true;
}
void TCCClient::river_output_dimensions(void *data, struct river_output_v1 *id,
                                        int32_t width, int32_t height) {
  TCCClient::Output *output = (TCCClient::Output *)data;
  output->width = (width < 1) ? 1 : width;
  output->height = (height < 1) ? 1 : height;
  output->geometry_changed = true;

  if (!output->client->mMainOutput) {
    output->client->mMainOutput = output;
    output->client->mMainOutputChanged = true;
  } else {
    if (output->width >= output->client->mMainOutput->width) {
      output->client->mMainOutput = output;
      output->client->mMainOutputChanged = true;
    }
  };
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

  if (seat->interacted->maximized || seat->interacted->fullscreen) {
    return;
  }

  if (seat->pointer_resize_edges && seat->pointer_window == seat->interacted) {
    seat->interacted->pointer_resize_requested = seat;
    seat->interacted->pointer_resize_requested_edges =
        seat->pointer_resize_edges;
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

  seat->wl_seat_id = (wl_seat *)wl_registry_bind(client->mRegistry, seat_id,
                                                 &wl_seat_interface, 1);
  wl_seat_add_listener(seat->wl_seat_id, &client->mWlSeatListener, seat);
}
void TCCClient::river_seat_shell_surface_interaction(
    void *data, struct river_seat_v1 *id,
    struct river_shell_surface_v1 *shell_surface) {}

void TCCClient::output_maybe_destroy(Output *output) {
  if (!output->removed) {
    return;
  }
  std::erase(mOutputs, output);
  mLock->output_removed(output);

  // Hand the windows over to another output so they don't end up stranded
  // off-screen, or stash them until one shows up if this was the last.
  Output *target = nullptr;
  for (Output *out : mOutputs) {
    if (!out->removed) {
      target = out;
      break;
    }
  }
  std::vector<Window *> windows;
  windows.insert(windows.end(), output->windows.begin(), output->windows.end());
  windows.insert(windows.end(), output->minimized_windows.begin(),
                 output->minimized_windows.end());
  output->windows.clear();
  output->minimized_windows.clear();
  for (Window *window : windows) {
    if (target) {
      // Not in any output's lists anymore, so move it in by hand.
      (window->minimized ? target->minimized_windows : target->windows)
          .push_back(window);
      window_transfer(window, output, target);
    } else {
      mOrphanedWindows.push_back(window);
    }
  }

  river_output_v1_destroy(output->id);
  delete output;
}

void TCCClient::output_adopt_orphans() {
  if (mOrphanedWindows.empty()) {
    return;
  }
  Output *target = output_under_pointer();
  if (!target) {
    return;
  }
  for (Window *window : std::vector<Window *>(mOrphanedWindows)) {
    window_move_to_output(window, target);
    window_transfer(window, nullptr, target);
  }
}

TCCClient::Output *TCCClient::output_of(Window *window) {
  for (Output *out : mOutputs) {
    if (std::find(out->windows.begin(), out->windows.end(), window) !=
            out->windows.end() ||
        std::find(out->minimized_windows.begin(), out->minimized_windows.end(),
                  window) != out->minimized_windows.end()) {
      return out;
    }
  }
  return nullptr;
}

TCCClient::Output *TCCClient::output_at(int x, int y) {
  for (Output *out : mOutputs) {
    if (!out->removed && out->contains(x, y)) {
      return out;
    }
  }
  return nullptr;
}

TCCClient::Output *TCCClient::output_under_pointer() {
  if (!mSeats.empty()) {
    Output *out = output_at(mSeats[0]->pointer_x, mSeats[0]->pointer_y);
    if (out) {
      return out;
    }
  }
  for (Output *out : mOutputs) {
    if (!out->removed) {
      return out;
    }
  }
  return nullptr;
}

void TCCClient::window_move_to_output(Window *window, Output *output) {
  Output *from = output_of(window);
  if (from == output) {
    return;
  }
  if (from) {
    std::erase(from->windows, window);
    std::erase(from->minimized_windows, window);
  } else {
    std::erase(mOrphanedWindows, window);
  }
  (window->minimized ? output->minimized_windows : output->windows)
      .push_back(window);
}

// Moves the window's position from one output's space over to another's,
// keeping it at the same place relative to the output. `from` is nullptr if
// that's unknown, in which case it's only made sure to be on `to`.
void TCCClient::window_transfer(Window *window, Output *from, Output *to) {
  if (window->maximized || window->fullscreen) {
    // Otherwise unmaximizing would put it back on the old output.
    if (from) {
      window->saved_x += to->x - from->x;
      window->saved_y += to->y - from->y;
    } else {
      window->saved_x = to->x + SSD_BORDER_SIZE;
      window->saved_y = to->y + SSD_BORDER_SIZE_TOP;
    }
    window_apply_geometry(window, to);
    return;
  }

  int x = window->x, y = window->y;
  if (from) {
    x += to->x - from->x;
    y += to->y - from->y;
  }
  // The new output might be smaller, keep the titlebar reachable.
  if (!to->contains(x, y)) {
    x = to->x + SSD_BORDER_SIZE;
    y = to->y + SSD_BORDER_SIZE_TOP;
  }
  window_set_position(window, x, y);
}

// Reassigns the window to whichever output its center is on, e.g. after it
// got dragged over to another one.
void TCCClient::window_update_output(Window *window) {
  Output *out =
      output_at(window->x + window->width / 2, window->y + window->height / 2);
  if (out) {
    window_move_to_output(window, out);
  }
}

// Fills the output with a maximized or fullscreen window, no-op otherwise.
void TCCClient::window_apply_geometry(Window *window, Output *out) {
  if (window->fullscreen) {
    window_apply_fullscreen(window, out);
  } else if (window->maximized) {
    window_apply_maximized(window, out);
  }
}

void TCCClient::window_apply_fullscreen(Window *window, Output *out) {
  window_set_position(window, out->x, out->y);
  river_window_v1_propose_dimensions(window->id, out->width, out->height);
  // The decoration sits outside the window content, so clipping to the
  // output's size cuts it off.
  river_window_v1_set_clip_box(window->id, 0, 0, out->width, out->height);
}

void TCCClient::window_apply_maximized(Window *window, Output *out) {
  if (window->has_decor) {
    window_set_position(window, out->x + SSD_BORDER_SIZE,
                        out->y + SSD_BORDER_SIZE_TOP);
    river_window_v1_propose_dimensions(
        window->id, out->width - (SSD_BORDER_SIZE * 2),
        out->height - SSD_BORDER_SIZE_TOP - SSD_BORDER_SIZE);
  } else {
    window_set_position(window, out->x, out->y);
    river_window_v1_propose_dimensions(window->id, out->width, out->height);
  }
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
      seat->pointer_resize_edges = 0;
    }
    if (seat->op_window == window) {
      river_seat_v1_op_end(seat->id);
      seat->op = SEAT_OP_NONE;
      seat->op_window = nullptr;
    }
  }

  window_destroy_nav_surfaces(window);
  window_destroy_resize_surfaces(window);
  river_window_v1_destroy(window->id);

  for (auto out : mOutputs) {
    std::erase(out->windows, window);
    std::erase(out->minimized_windows, window);
  }
  std::erase(mOrphanedWindows, window);
  delete window;
}

void TCCClient::window_set_position(Window *window, int32_t x, int32_t y) {
  river_node_v1_set_position(window->node, x, y);
  window->x = x;
  window->y = y;
}

// Maximized and fullscreen are independent, e.g. a maximized window that goes
// fullscreen comes back maximized. Fullscreen is just the window resized to
// cover its output with the decoration clipped away.
void TCCClient::window_set_state(Window *window, bool maximized,
                                 bool fullscreen, Output *fullscreen_output) {
  Output *out = output_of(window);
  if (!out) {
    return;
  }
  if (maximized == window->maximized && fullscreen == window->fullscreen) {
    return;
  }

  bool was_floating = !window->maximized && !window->fullscreen;
  if (was_floating) {
    window->saved_x = window->x;
    window->saved_y = window->y;
    window->saved_width = window->width;
    window->saved_height = window->height;
  }

  if (fullscreen != window->fullscreen) {
    if (fullscreen) {
      if (fullscreen_output && fullscreen_output != out) {
        window_move_to_output(window, fullscreen_output);
        // So leaving fullscreen puts it back on the output it's now on.
        window->saved_x += fullscreen_output->x - out->x;
        window->saved_y += fullscreen_output->y - out->y;
        out = fullscreen_output;
      }
      river_window_v1_inform_fullscreen(window->id);
      river_node_v1_place_top(window->node);
    } else {
      river_window_v1_inform_not_fullscreen(window->id);
      river_window_v1_set_clip_box(window->id, 0, 0, 0, 0);
    }
  }
  if (maximized != window->maximized) {
    if (maximized) {
      river_window_v1_inform_maximized(window->id);
    } else {
      river_window_v1_inform_unmaximized(window->id);
    }
  }
  window->maximized = maximized;
  window->fullscreen = fullscreen;

  if (fullscreen || maximized) {
    window_apply_geometry(window, out);
  } else if (window->saved_width > 0) {
    window_set_position(window, window->saved_x, window->saved_y);
    river_window_v1_propose_dimensions(window->id, window->saved_width,
                                       window->saved_height);
  } else {
    // Never had a size of its own, let it pick one and center it.
    window->center_requested = true;
    river_window_v1_propose_dimensions(window->id, 0, 0);
  }
}

void TCCClient::window_minimize(Window *window) {
  Output *out = output_of(window);
  if (!out) {
    return;
  }
  window->minimized = !window->minimized;
  window->want_minimized = false;
  if (window->minimized) {
    std::erase(out->windows, window);
    out->minimized_windows.push_back(window);
    river_window_v1_hide(window->id);
    // Don't leave keyboard input going to a hidden window. seat_manage picks
    // the next window to focus.
    for (Seat *seat : mSeats) {
      if (seat->focused == window) {
        seat_focus(seat, nullptr);
      }
    }
  } else {
    std::erase(out->minimized_windows, window);
    out->windows.push_back(window);
    river_window_v1_show(window->id);
  }
}

void TCCClient::window_manage(Window *window) {
  if (window->is_new) {
    window->is_new = false;
    river_window_v1_set_capabilities(
        window->id, RIVER_WINDOW_V1_CAPABILITIES_MAXIMIZE |
                        RIVER_WINDOW_V1_CAPABILITIES_MINIMIZE |
                        RIVER_WINDOW_V1_CAPABILITIES_FULLSCREEN);

    if (window->has_decor) {
      window->center_requested = true;
      river_window_v1_hide(window->id); /* hide the window so that we don't see
                                           it in its initial position */

      window->setup_decor();
      window->decor_draw();
    } else {
      Output *out = output_of(window);
      window_set_position(window, out ? out->x : 0, out ? out->y : 0);
    }
    river_window_v1_propose_dimensions(window->id, 0, 0);

    // Focus new windows as they open. seat_manage runs after this and does the
    // actual focusing, a click in the same manage sequence takes priority.
    for (Seat *seat : mSeats) {
      if (!seat->interacted) {
        seat->interacted = window;
      }
    }
  }
  switch (window->pending_nav_action) {
  case NAV_BUTTON_CLOSE:
    river_window_v1_close(window->id);
    break;
  case NAV_BUTTON_MAX:
    window->want_maximized = !window->maximized;
    break;
  case NAV_BUTTON_MIN:
    window->want_minimized = true;
    break;
  default:
    break;
  }
  window->pending_nav_action = NAV_BUTTON_NONE;

  if (window->pointer_move_requested != nullptr) {
    if (!window->fullscreen) {
      // Dragging a maximized window off unmaximizes it.
      window->want_maximized = false;
      window_set_state(window, false, false);
      seat_pointer_move(window->pointer_move_requested, window);
    }
    window->pointer_move_requested = nullptr;
  }
  if (window->pointer_resize_requested != nullptr) {
    if (!window->maximized && !window->fullscreen) {
      seat_pointer_resize(window->pointer_resize_requested, window,
                          window->pointer_resize_requested_edges);
    }
    window->pointer_resize_requested = nullptr;
  }

  if (window->want_maximized != window->maximized ||
      window->want_fullscreen != window->fullscreen) {
    Output *fullscreen_output = nullptr;
    for (Output *o : mOutputs) {
      if (!o->removed && o->id == window->want_fullscreen_output) {
        fullscreen_output = o;
      }
    }
    window_set_state(window, window->want_maximized, window->want_fullscreen,
                     fullscreen_output);
  }
  window->want_fullscreen_output = nullptr;

  // Last, since it takes the window out of the output's window list.
  if (window->want_minimized && !window->minimized) {
    window_minimize(window);
  }

  // Orphaned windows wait for an output before getting centered on it.
  Output *out = output_of(window);
  if (window->center_requested && out && window->width > 0 &&
      window->height > 0) {
    // A window that started out maximized/fullscreen is already where it
    // should be, it only needs showing.
    if (!window->maximized && !window->fullscreen) {
      // Undecorated windows can end up here too, after being unmaximized
      // without ever having had a size of their own.
      int border_x = window->has_decor ? SSD_BORDER_SIZE : 0;
      int border_y = window->has_decor ? SSD_BORDER_SIZE_TOP : 0;
      window_set_position(
          window, out->x + ((out->width / 2) - (window->width / 2)) + border_x,
          out->y + ((out->height / 2) - (window->height / 2)) + border_y);
    }
    window->center_requested = false;
    river_window_v1_show(window->id);

    if (window->has_decor) {
      window->decor_draw();
    }
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
    if (wl_pointer_get_version(seat->wl_pointer_id) >=
        WL_POINTER_RELEASE_SINCE_VERSION)
      wl_pointer_release(seat->wl_pointer_id);
    else
      wl_pointer_destroy(seat->wl_pointer_id);
  }
  if (seat->wl_seat_id) {
    mLock->seat_removed(seat->wl_seat_id);
    if (wl_seat_get_version(seat->wl_seat_id) >= WL_SEAT_RELEASE_SINCE_VERSION)
      wl_seat_release(seat->wl_seat_id);
    else
      wl_seat_destroy(seat->wl_seat_id);
  }

  river_seat_v1_destroy(seat->id);
  std::erase(mSeats, seat);
  delete seat;
}

// Passing nullptr clears focus.
void TCCClient::seat_focus(Seat *seat, Window *window) {
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
  // seat_focus(seat, window);
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
  // seat_focus(seat, window);
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

void TCCClient::launch_component(std::string name) {
  char dest[PATH_MAX];
  memset(dest, 0, sizeof(dest));
  if (readlink("/proc/self/exe", dest, PATH_MAX) == -1) {
    perror("readlink");
  }
  std::filesystem::path fs = dest;

  std::filesystem::path path = fs.parent_path() / name;

  printf("%s\n", path.string().c_str());

  const char *args[] = {path.c_str(), NULL};
  spawn(path.c_str(), args);
}

void TCCClient::launch_kwallet() {
  // pam_kwallet starts ksecretd at login with the password, which then waits
  // on this socket for the session's environment before unlocking the wallet
  // and going on the session bus. Plasma sends it with pam_kwallet_init (env |
  // socat), done here directly so it doesn't need socat or a libexec path.
  const char *socket_path = getenv("PAM_KWALLET5_LOGIN");
  if (socket_path == nullptr) {
    // Not logged in through pam_kwallet, so the wallet will have to be
    // unlocked with a prompt.
    const char *args[] = {"ksecretd", nullptr};
    spawn("ksecretd", args);
    return;
  }

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (strlen(socket_path) >= sizeof(addr.sun_path)) {
    fprintf(stderr, "kwallet: socket path too long: %s\n", socket_path);
    return;
  }
  strcpy(addr.sun_path, socket_path);

  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    perror("kwallet: socket");
    return;
  }
  if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("kwallet: connect");
    close(fd);
    return;
  }

  std::string env;
  for (char **var = environ; *var != nullptr; var++) {
    env += *var;
    env += '\n';
  }
  size_t sent = 0;
  while (sent < env.size()) {
    ssize_t n = send(fd, env.data() + sent, env.size() - sent, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      perror("kwallet: send");
      break;
    }
    sent += n;
  }
  close(fd);
}

void TCCClient::spawn(const char *path, const char *const argv[]) {
  pid_t pid = fork();
  if (pid == 0) {
    // execvp resets our SIGCHLD handler to the default
    execvp(path, (char **)argv);
    _exit(1);
  }
  if (pid > 0)
    mChildren.push_back(pid);
  else
    perror("fork");
}

void TCCClient::reap_children() {
  // only wait on our own children, others (e.g. glycin's) belong to whoever
  // spawned them
  std::erase_if(mChildren,
                [](pid_t pid) { return waitpid(pid, nullptr, WNOHANG) != 0; });
}
void TCCClient::seat_action(Seat *seat, Action action) {
  switch (action) {
  case ACTION_NONE:
    break;
  case ACTION_SPAWN_TERMINAL: {
    const char *args[] = {"konsole", nullptr};
    spawn("konsole", args);
    break;
  }
  // through WirePlumber, whose -l keeps volume up from going past 100%
  case ACTION_VOLUME_UP: {
    const char *args[] = {"wpctl", "set-volume",           "-l",
                          "1.0",   "@DEFAULT_AUDIO_SINK@", "5%+",
                          nullptr};
    spawn("wpctl", args);
    break;
  }
  case ACTION_VOLUME_DOWN: {
    const char *args[] = {"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%-",
                          nullptr};
    spawn("wpctl", args);
    break;
  }
  case ACTION_VOLUME_MUTE: {
    const char *args[] = {"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle",
                          nullptr};
    spawn("wpctl", args);
    break;
  }
  case ACTION_MIC_MUTE: {
    const char *args[] = {"wpctl", "set-mute", "@DEFAULT_AUDIO_SOURCE@",
                          "toggle", nullptr};
    spawn("wpctl", args);
    break;
  }
  case ACTION_LOCK: {
    printf("attempting lock\n");
    lock();
    break;
  }
  case ACTION_CLOSE:
    if (seat->focused != nullptr) {
      river_window_v1_close(seat->focused->id);
    }
    break;
  case ACTION_FOCUS_NEXT: {
    Output *out = output_under_pointer();
    if (out && !out->windows.empty()) {
      // Focus the bottom window
      seat_focus(seat, out->windows.front());
    }
    break;
  }
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
  case ACTION_SPAWN_SIGSEGV: {
    printf("segfault spawn?\n");
    void (*func)() = nullptr;
    func();
    break;
  }
  }
}

void TCCClient::seat_manage(Seat *seat) {
  if (seat->is_new) {
    seat->is_new = false;
    const uint32_t alt = RIVER_SEAT_V1_MODIFIERS_MOD1;
    const uint32_t super = RIVER_SEAT_V1_MODIFIERS_MOD4;
    xkb_binding_create(seat, super, XKB_KEY_space, ACTION_SPAWN_TERMINAL);
    xkb_binding_create(seat, super, XKB_KEY_q, ACTION_CLOSE);
    xkb_binding_create(seat, alt, XKB_KEY_Tab, ACTION_FOCUS_NEXT);
    xkb_binding_create(seat, super, XKB_KEY_Escape, ACTION_EXIT);
    pointer_binding_create(seat, super, BTN_LEFT, ACTION_MOVE);
    pointer_binding_create(seat, super, BTN_RIGHT, ACTION_RESIZE);
    xkb_binding_create(seat, super, XKB_KEY_space, ACTION_SPAWN_TERMINAL);

    xkb_binding_create(seat, 0, XKB_KEY_XF86AudioRaiseVolume, ACTION_VOLUME_UP);
    xkb_binding_create(seat, 0, XKB_KEY_XF86AudioLowerVolume,
                       ACTION_VOLUME_DOWN);
    xkb_binding_create(seat, 0, XKB_KEY_XF86AudioMute, ACTION_VOLUME_MUTE);
    xkb_binding_create(seat, 0, XKB_KEY_XF86AudioMicMute, ACTION_MIC_MUTE);
    xkb_binding_create(seat, super, XKB_KEY_l, ACTION_LOCK);

    /*
     * binding for testing what the window manager does when it segfaults.
     * should be a combination that NOBODY would reasonably hit.
     */
    xkb_binding_create(
        seat,
        RIVER_SEAT_V1_MODIFIERS_SHIFT | RIVER_SEAT_V1_MODIFIERS_CTRL |
            RIVER_SEAT_V1_MODIFIERS_MOD1 | RIVER_SEAT_V1_MODIFIERS_MOD4,
        XKB_KEY_F2, ACTION_SPAWN_SIGSEGV);
  }

  // Only touch focus when a window was interacted with, or the focused one
  // went away (closed/minimized), in which case the top window takes over.
  // Otherwise every unrelated manage sequence would steal focus.
  if (seat->interacted) {
    seat_focus(seat, seat->interacted);
  } else if (!seat->focused) {
    Output *out = output_under_pointer();
    seat_focus(seat,
               (out && !out->windows.empty()) ? out->windows.back() : nullptr);
  }

  seat->interacted = nullptr;

  seat_action(seat, seat->pending_action);
  seat->pending_action = ACTION_NONE;

  switch (seat->op) {
  case SEAT_OP_NONE:
    break;
  case SEAT_OP_MOVE:
    if (seat->op_release) {
      window_update_output(seat->op_window);
      river_seat_v1_op_end(seat->id);
      seat->op = SEAT_OP_NONE;
      seat->op_window = nullptr;
    }
    break;
  case SEAT_OP_RESIZE: {
    if (seat->op_release) {
      river_window_v1_inform_resize_end(seat->op_window->id);
      window_update_output(seat->op_window);
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
    Window *window = seat->op_window;
    if (window->min_width > 0 && width < window->min_width) {
      width = window->min_width;
    }
    if (window->min_height > 0 && height < window->min_height) {
      height = window->min_height;
    }
    if (window->max_width > 0 && width > window->max_width) {
      width = window->max_width;
    }
    if (window->max_height > 0 && height > window->max_height) {
      height = window->max_height;
    }
    river_window_v1_propose_dimensions(window->id, width, height);
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

bool TCCClient::Window::nav_button_visible(int button) const {
  return button != NAV_BUTTON_MAX || show_maximize;
}

int TCCClient::Window::nav_button_x_from_right(int button) const {
  if (button == NAV_BUTTON_MIN && !show_maximize) {
    return SSD_NAV_BUTTON_MAX_X_FROM_RIGHT;
  }
  return ::nav_button_x_from_right[button];
}

uint8_t TCCClient::get_pressed_nav_button(Seat *seat, Window *window) {
  // Pointer position in decoration surface coordinates.
  int x = seat->pointer_x - window->x + SSD_BORDER_SIZE;
  int y = seat->pointer_y - window->y + SSD_BORDER_SIZE_TOP;
  if (y < SSD_NAV_BUTTON_Y || y >= SSD_NAV_BUTTON_Y + SSD_NAV_BUTTON_HEIGHT) {
    return NAV_BUTTON_NONE;
  }

  for (int i = NAV_BUTTON_NONE + 1; i < NAV_BUTTON_COUNT; i++) {
    if (!window->nav_button_visible(i)) {
      continue;
    }
    int lo = window->decor_width - window->nav_button_x_from_right(i);
    if (x >= lo && x < lo + SSD_NAV_BUTTON_WIDTH) {
      return i;
    }
  }
  return NAV_BUTTON_NONE;
}

wl_buffer *TCCClient::create_transparent_buffer(int width, int height) {
  const int stride = width * 4;
  const int size = stride * height;

  // Every buffer starts at offset 0 of the same pool. ftruncate zero-fills,
  // which is exactly the fully transparent contents we want, and nothing ever
  // writes to it, so there's no need to mmap it.
  if (size > mTransparentPoolSize) {
    if (mTransparentPoolFd < 0) {
      mTransparentPoolFd = memfd_create("tcc-transparent", MFD_CLOEXEC);
    }
    if (mTransparentPoolFd < 0 || ftruncate(mTransparentPoolFd, size) < 0) {
      perror("transparent buffer");
      raise(SIGTRAP);
    }

    if (mTransparentPool) {
      wl_shm_pool_resize(mTransparentPool, size);
    } else {
      mTransparentPool = wl_shm_create_pool(mShm, mTransparentPoolFd, size);
    }
    mTransparentPoolSize = size;
  }

  return wl_shm_pool_create_buffer(mTransparentPool, 0, width, height, stride,
                                   WL_SHM_FORMAT_ARGB8888);
}

wl_buffer *TCCClient::get_nav_button_buffer() {
  if (!mNavButtonBuffer) {
    mNavButtonBuffer =
        create_transparent_buffer(SSD_NAV_BUTTON_WIDTH, SSD_NAV_BUTTON_HEIGHT);
  }
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
    nav.mapped = true;
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
    auto &nav = window->nav_surfaces[i];
    if (!nav.subsurface) {
      continue;
    }
    // Unmap hidden buttons so they don't eat input meant for whatever took
    // their slot.
    bool visible = window->nav_button_visible(i);
    if (visible != nav.mapped) {
      wl_surface_attach(nav.surface,
                        visible ? get_nav_button_buffer() : nullptr, 0, 0);
      wl_surface_commit(nav.surface);
      nav.mapped = visible;
    }
    wl_subsurface_set_position(nav.subsurface,
                               window->decor_width -
                                   window->nav_button_x_from_right(i),
                               SSD_NAV_BUTTON_Y);
  }
}

// Resize surface buffers change size along with the window, so each one is
// only used for a single attach and gets destroyed once the compositor is done
// with it.
static void resize_buffer_release(void *data, wl_buffer *buffer) {
  wl_buffer_destroy(buffer);
}
static const wl_buffer_listener resize_buffer_listener = {
    .release = resize_buffer_release,
};

void TCCClient::window_create_resize_surfaces(Window *window) {
  if (!mSubcompositor || !mShm) {
    fprintf(stderr, "wl_subcompositor or wl_shm not supported, windows "
                    "won't be resizable from their borders\n");
    return;
  }

  // Created in ResizeSurface order, so the corners end up stacked on top.
  for (auto &resize : window->resize_surfaces) {
    resize.surface = wl_compositor_create_surface(mCompositor);
    resize.subsurface = wl_subcompositor_get_subsurface(
        mSubcompositor, resize.surface, window->decor_surface);
  }
  // Buffers are attached in window_position_resize_surfaces, once we know the
  // decoration's size.
}

void TCCClient::window_destroy_resize_surfaces(Window *window) {
  for (auto &resize : window->resize_surfaces) {
    if (resize.subsurface) {
      wl_subsurface_destroy(resize.subsurface);
    }
    if (resize.surface) {
      wl_surface_destroy(resize.surface);
    }
    resize = {};
  }
}

void TCCClient::window_position_resize_surfaces(Window *window) {
  // In decoration surface coordinates. Each surface covers the border and
  // sticks SSD_BORDER_LEEWAY out past it.
  const int t = SSD_RESIZE_THICKNESS;
  const int outer = -SSD_BORDER_LEEWAY;
  const int right = window->decor_width - SSD_BORDER_SIZE;
  const int bottom = window->decor_height - SSD_BORDER_SIZE;
  const int long_w = window->decor_width + SSD_BORDER_LEEWAY * 2;
  const int long_h = window->decor_height + SSD_BORDER_LEEWAY * 2;

  struct {
    int x, y, width, height;
  } rects[RESIZE_SURFACE_COUNT] = {
      // Same order as ResizeSurface.
      {outer, outer, long_w, t}, {outer, bottom, long_w, t},
      {outer, outer, t, long_h}, {right, outer, t, long_h},
      {outer, outer, t, t},      {right, outer, t, t},
      {outer, bottom, t, t},     {right, bottom, t, t},
  };

  for (int i = 0; i < RESIZE_SURFACE_COUNT; i++) {
    auto &resize = window->resize_surfaces[i];
    if (!resize.subsurface) {
      continue;
    }

    // Maximized and fullscreen windows can't be resized, so unmap the
    // surfaces entirely.
    bool fixed = window->maximized || window->fullscreen;
    int width = fixed ? 0 : rects[i].width;
    int height = fixed ? 0 : rects[i].height;

    // Like the nav surfaces, these are synchronized, so the new size and
    // position land with the decoration's commit.
    wl_subsurface_set_position(resize.subsurface, rects[i].x, rects[i].y);
    if (width == resize.width && height == resize.height) {
      continue;
    }

    wl_buffer *buffer = nullptr;
    if (width > 0 && height > 0) {
      buffer = create_transparent_buffer(width, height);
      wl_buffer_add_listener(buffer, &resize_buffer_listener, nullptr);
    }
    wl_surface_attach(resize.surface, buffer, 0, 0);
    wl_surface_damage(resize.surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(resize.surface);
    resize.width = width;
    resize.height = height;
  }
}
