#ifdef TCC_SYSTRAY_RIVER
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
  X(wl_display_disconnect)                                                     \
  X(wl_proxy_marshal_array_flags)                                              \
  X(wl_proxy_add_listener)                                                     \
  X(wl_proxy_destroy)                                                          \
  X(wl_proxy_get_version)                                                      \
  X(wl_proxy_get_interface)

// Function table for a dynamically loaded libwayland-client. Each member has
// the same name and signature as the libwayland-client function it points to.
//
// The inline request wrappers in the protocol headers (wl_registry_bind etc.)
// call wl_proxy_* functions directly, so wayland_loader.cpp also defines those
// functions, forwarding to this table.
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
#endif
