#include "client.hpp"

#include <vector>

void TCCClient::xkb_binding_create(Seat *seat, uint32_t mods,
                                   xkb_keysym_t keysym, Action action) {
  std::shared_ptr<XkbBinding> binding = std::make_shared<XkbBinding>();
  auto binding_ptr = binding.get();
  binding_ptr->client = this->shared_from_this();
  binding_ptr->id = river_xkb_bindings_v1_get_xkb_binding(
      mRiverXKBBinding, seat->id, keysym, mods);
  binding_ptr->seat = seat;
  binding_ptr->action = action;

  river_xkb_binding_v1_add_listener(binding_ptr->id, &mRiverXkbBindingListener,
                                    binding_ptr);
  river_xkb_binding_v1_enable(binding_ptr->id);

  seat->xkb_bindings.push_back(binding);
}

void TCCClient::xkb_binding_destroy(std::shared_ptr<XkbBinding> binding) {
  river_xkb_binding_v1_destroy(binding->id);
  std::erase(binding->seat->xkb_bindings, binding);
  // delete binding;
}

void TCCClient::pointer_binding_create(Seat *seat, uint32_t mods,
                                       uint32_t button, Action action) {
  std::shared_ptr<PointerBinding> binding = std::make_shared<PointerBinding>();
  binding->client = this->shared_from_this();
  binding->id = river_seat_v1_get_pointer_binding(seat->id, button, mods);
  binding->seat = seat;
  binding->action = action;

  river_pointer_binding_v1_add_listener(
      binding->id, &mRiverPointerBindingListener, binding.get());
  river_pointer_binding_v1_enable(binding->id);

  seat->pointer_bindings.push_back(binding);
}

void TCCClient::pointer_binding_destroy(
    std::shared_ptr<PointerBinding> binding) {
  river_pointer_binding_v1_destroy(binding->id);
  std::erase(binding->seat->pointer_bindings, binding);
}

void TCCClient::river_xkb_binding_pressed(
    void *data, struct river_xkb_binding_v1 *river_xkb_binding_v1) {
  XkbBinding *binding = (XkbBinding *)data;
  binding->seat->pending_action = binding->action;
}

void TCCClient::river_pointer_binding_pressed(
    void *data, struct river_pointer_binding_v1 *river_pointer_binding_v1) {
  PointerBinding *binding = (PointerBinding *)data;
  binding->seat->pending_action = binding->action;
}

// Ignored events
void TCCClient::river_xkb_binding_released(
    void *data, struct river_xkb_binding_v1 *river_xkb_binding_v1) {}
void TCCClient::river_xkb_binding_stop_repeat(
    void *data, struct river_xkb_binding_v1 *river_xkb_binding_v1) {}

// Ignored event
void TCCClient::river_pointer_binding_released(
    void *data, struct river_pointer_binding_v1 *river_pointer_binding_v1) {}

void TCCClient::river_input_finished(
    void *data, struct river_input_manager_v1 *river_input_manager_v1) {}

void TCCClient::river_input_input_device(
    void *data, struct river_input_manager_v1 *river_input_manager_v1,
    struct river_input_device_v1 *id) {}

TCCClient::Window *TCCClient::window_from_decor_surface(wl_surface *surface) {
  for (Window *window : mWindows) {
    if (window->has_decor && window->main_decor.surface == surface) {
      return window;
    }
  }
  return nullptr;
}

void TCCClient::wl_seat_capabilities(void *data, struct wl_seat *wl_seat,
                                     uint32_t capabilities) {
  Seat *seat = (Seat *)data;
  bool has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;

  if (has_pointer && !seat->wl_pointer_id) {
    seat->wl_pointer_id = wl_seat_get_pointer(wl_seat);
    wl_pointer_add_listener(seat->wl_pointer_id,
                            &seat->client->mWlPointerListener, seat);

    if (seat->client->mCursorShapeManager) {
      seat->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(
          seat->client->mCursorShapeManager, seat->wl_pointer_id);
    }
  } else if (!has_pointer && seat->wl_pointer_id) {
    if (seat->cursor_shape_device) {
      wp_cursor_shape_device_v1_destroy(seat->cursor_shape_device);
      seat->cursor_shape_device = nullptr;
    }
    wl_pointer_release(seat->wl_pointer_id);
    seat->wl_pointer_id = nullptr;
    seat->pointer_window = nullptr;
  }
}

void TCCClient::wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *surface,
                                 wl_fixed_t surface_x, wl_fixed_t surface_y) {
  Seat *seat = (Seat *)data;
  seat->pointer_window = seat->client->window_from_decor_surface(surface);
  uint32_t edges = seat->client->get_pointer_edges(
      seat, seat->pointer_window, wl_fixed_to_double(surface_x),
      wl_fixed_to_double(surface_y));

  uint32_t cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
  if (edges) {
    if (edges == RIVER_WINDOW_V1_EDGES_RIGHT)
      cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE;
    if (edges == (RIVER_WINDOW_V1_EDGES_BOTTOM))
      cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE;
    if (edges == (RIVER_WINDOW_V1_EDGES_BOTTOM | RIVER_WINDOW_V1_EDGES_RIGHT))
      cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE;
  }
  wp_cursor_shape_device_v1_set_shape(seat->cursor_shape_device, serial,
                                      cursor_shape);
}

void TCCClient::wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *surface) {
  Seat *seat = (Seat *)data;
  seat->pointer_window = nullptr;
  wp_cursor_shape_device_v1_set_shape(seat->cursor_shape_device, serial,
                                      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
}

void TCCClient::wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t time, wl_fixed_t surface_x,
                                  wl_fixed_t surface_y) {

  Seat *seat = (Seat *)data;
}

// Ignored events
void TCCClient::wl_seat_name(void *data, struct wl_seat *wl_seat,
                             const char *name) {}
void TCCClient::wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t serial, uint32_t time,
                                  uint32_t button, uint32_t state) {}
void TCCClient::wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
                                uint32_t time, uint32_t axis,
                                wl_fixed_t value) {}
void TCCClient::wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {}
void TCCClient::wl_pointer_axis_source(void *data,
                                       struct wl_pointer *wl_pointer,
                                       uint32_t axis_source) {}
void TCCClient::wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                                     uint32_t time, uint32_t axis) {}
void TCCClient::wl_pointer_axis_discrete(void *data,
                                         struct wl_pointer *wl_pointer,
                                         uint32_t axis, int32_t discrete) {}
void TCCClient::wl_pointer_axis_value120(void *data,
                                         struct wl_pointer *wl_pointer,
                                         uint32_t axis, int32_t value120) {}
