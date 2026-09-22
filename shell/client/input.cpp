#include "client.hpp"

#include <vector>

void TCCClient::xkb_binding_create(Seat *seat, uint32_t mods,
                                   xkb_keysym_t keysym, Action action) {
  std::shared_ptr<XkbBinding> binding = std::make_shared<XkbBinding>();
  auto binding_ptr = binding.get();
  binding_ptr->client = this;
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
  PointerBinding *binding = new PointerBinding();
  binding->client = this;
  binding->id = river_seat_v1_get_pointer_binding(seat->id, button, mods);
  binding->seat = seat;
  binding->action = action;

  river_pointer_binding_v1_add_listener(binding->id,
                                        &mRiverPointerBindingListener, binding);
  river_pointer_binding_v1_enable(binding->id);

  seat->pointer_bindings.push_back(binding);
}

void TCCClient::pointer_binding_destroy(PointerBinding *binding) {
  river_pointer_binding_v1_destroy(binding->id);
  std::erase(binding->seat->pointer_bindings, binding);
  delete binding;
}

void TCCClient::river_xkb_binding_pressed(
    void *data, struct river_xkb_binding_v1 *river_xkb_binding_v1) {
  XkbBinding *binding = (XkbBinding *)data;
  binding->seat->pending_action = binding->action;
}

// Ignored events
void TCCClient::river_xkb_binding_released(
    void *data, struct river_xkb_binding_v1 *river_xkb_binding_v1) {}
void TCCClient::river_xkb_binding_stop_repeat(
    void *data, struct river_xkb_binding_v1 *river_xkb_binding_v1) {}

void TCCClient::river_pointer_binding_pressed(
    void *data, struct river_pointer_binding_v1 *river_pointer_binding_v1) {
  PointerBinding *binding = (PointerBinding *)data;
  binding->seat->pending_action = binding->action;
}

// Ignored event
void TCCClient::river_pointer_binding_released(
    void *data, struct river_pointer_binding_v1 *river_pointer_binding_v1) {}

void TCCClient::river_input_finished(
    void *data, struct river_input_manager_v1 *river_input_manager_v1) {}

void TCCClient::river_input_input_device(
    void *data, struct river_input_manager_v1 *river_input_manager_v1,
    struct river_input_device_v1 *id) {}
