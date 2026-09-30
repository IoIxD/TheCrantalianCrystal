#pragma once

#include <wayland-client-core.h>

// namespace {
struct wl_display *wl_display_connect(const char *name);
int wl_display_roundtrip(struct wl_display *display);
int wl_display_dispatch_pending(struct wl_display *display);
int wl_display_prepare_read(struct wl_display *display);
void wl_display_cancel_read(struct wl_display *display);
int wl_display_read_events(struct wl_display *display);
int wl_display_flush(struct wl_display *display);
int wl_display_get_fd(struct wl_display *display);
int wl_display_get_error(struct wl_display *display);
struct wl_proxy *
wl_proxy_marshal_array_flags(struct wl_proxy *proxy, uint32_t opcode,
                             const struct wl_interface *interface,
                             uint32_t version, uint32_t flags,
                             union wl_argument *args);
int wl_proxy_add_listener(struct wl_proxy *proxy, void (**implementation)(void),
                          void *data);
void wl_proxy_destroy(struct wl_proxy *proxy);
uint32_t wl_proxy_get_version(struct wl_proxy *proxy);
void *wl_proxy_get_user_data(struct wl_proxy *proxy);
const struct wl_interface *wl_proxy_get_interface(struct wl_proxy *proxy);
void wl_display_disconnect(struct wl_display *display);
struct wl_proxy *wl_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode,
                                        const struct wl_interface *interface,
                                        uint32_t version, uint32_t flags, ...);
void wl_proxy_set_user_data(struct wl_proxy *proxy, void *user_data);

// } // namespace

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
  X(wl_proxy_get_interface)                                                    \
  X(wl_display_disconnect)                                                     \
  X(wl_proxy_marshal_flags)                                                    \
  X(wl_proxy_set_user_data)

typedef struct WaylandLib {
#define X(name) decltype(&::name) name;
  TCC_WAYLAND_FUNCS(X)
#undef X

  void *handle;
} WaylandLib;

extern WaylandLib *WL_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define wl_display_connect WL_LIB->wl_display_connect
#define wl_display_roundtrip WL_LIB->wl_display_roundtrip
#define wl_display_dispatch_pending WL_LIB->wl_display_dispatch_pending
#define wl_display_prepare_read WL_LIB->wl_display_prepare_read
#define wl_display_read_events WL_LIB->wl_display_read_events
#define wl_display_cancel_read WL_LIB->wl_display_cancel_read
#define wl_display_flush WL_LIB->wl_display_flush
#define wl_display_get_fd WL_LIB->wl_display_get_fd
#define wl_display_get_error WL_LIB->wl_display_get_error
#define wl_proxy_marshal_array_flags WL_LIB->wl_proxy_marshal_array_flags
#define wl_proxy_add_listener WL_LIB->wl_proxy_add_listener
#define wl_proxy_destroy WL_LIB->wl_proxy_destroy
#define wl_proxy_get_version WL_LIB->wl_proxy_get_version
#define wl_proxy_get_user_data WL_LIB->wl_proxy_get_user_data
#define wl_proxy_get_interface WL_LIB->wl_proxy_get_interface
#define wl_display_disconnect WL_LIB->wl_display_disconnect
#define wl_proxy_marshal_flags WL_LIB->wl_proxy_marshal_flags
#define wl_proxy_set_user_data WL_LIB->wl_proxy_set_user_data
#endif
