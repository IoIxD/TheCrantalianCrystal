#pragma once

#include <wayland-client.h>

#define TCC_WAYLAND_FUNCS(X)                                                   \
  X(wl_display_connect)                                                        \
  X(wl_display_roundtrip)                                                      \
  X(wl_display_dispatch_pending)                                               \
  X(wl_display_prepare_read)                                                   \
  X(wl_display_read_events)                                                    \
  X(wl_display_cancel_read)                                                    \
  X(wl_display_flush)                                                          \
  X(wl_display_get_fd)                                                         \
  X(wl_display_get_error)                                                      \
  X(wl_proxy_marshal_array_flags)                                              \
  X(wl_proxy_add_listener)                                                     \
  X(wl_proxy_destroy)                                                          \
  X(wl_proxy_get_version)                                                      \
  X(wl_proxy_get_user_data)                                                    \
  X(wl_proxy_get_interface)

struct WaylandLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_WAYLAND_FUNCS(X)
#undef X

  // Returns the process-wide instance, or nullptr if libwayland-client could
  // not be loaded.
  static WaylandLib *get();

private:
  void *mHandle = nullptr;
  bool load();
};
