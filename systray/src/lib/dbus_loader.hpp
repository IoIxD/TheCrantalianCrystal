#ifdef TCC_SYSTRAY_DBUS
#pragma once

#include <dbus/dbus.h>

#define TCC_DBUS_FUNCS(X)                                                      \
  X(dbus_error_init)                                                           \
  X(dbus_error_free)                                                           \
  X(dbus_error_is_set)                                                         \
  X(dbus_bus_get_private)                                                      \
  X(dbus_bus_get_unique_name)                                                  \
  X(dbus_bus_request_name)                                                     \
  X(dbus_bus_name_has_owner)                                                   \
  X(dbus_bus_add_match)                                                        \
  X(dbus_connection_set_exit_on_disconnect)                                    \
  X(dbus_connection_add_filter)                                                \
  X(dbus_connection_read_write)                                                \
  X(dbus_connection_dispatch)                                                  \
  X(dbus_connection_send)                                                      \
  X(dbus_connection_send_with_reply_and_block)                                 \
  X(dbus_connection_flush)                                                     \
  X(dbus_connection_get_unix_fd)                                               \
  X(dbus_connection_close)                                                     \
  X(dbus_connection_unref)                                                     \
  X(dbus_message_new_method_call)                                              \
  X(dbus_message_new_method_return)                                            \
  X(dbus_message_new_error)                                                    \
  X(dbus_message_new_signal)                                                   \
  X(dbus_message_unref)                                                        \
  X(dbus_message_set_no_reply)                                                 \
  X(dbus_message_append_args)                                                  \
  X(dbus_message_get_args)                                                     \
  X(dbus_message_get_type)                                                     \
  X(dbus_message_get_sender)                                                   \
  X(dbus_message_get_path)                                                     \
  X(dbus_message_get_interface)                                                \
  X(dbus_message_get_member)                                                   \
  X(dbus_message_is_signal)                                                    \
  X(dbus_message_iter_init)                                                    \
  X(dbus_message_iter_init_append)                                             \
  X(dbus_message_iter_open_container)                                          \
  X(dbus_message_iter_close_container)                                         \
  X(dbus_message_iter_append_basic)                                            \
  X(dbus_message_iter_get_arg_type)                                            \
  X(dbus_message_iter_recurse)                                                 \
  X(dbus_message_iter_get_basic)                                               \
  X(dbus_message_iter_next)                                                    \
  X(dbus_message_iter_get_fixed_array)                                         \
  X(dbus_connection_send_with_reply)                                           \
  X(dbus_pending_call_set_notify)                                              \
  X(dbus_pending_call_steal_reply)                                             \
  X(dbus_pending_call_cancel)                                                  \
  X(dbus_pending_call_unref)

// Function table for a dynamically loaded libdbus-1. Each member has the same
// name and signature as the libdbus function it points to.
struct DBusLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_DBUS_FUNCS(X)
#undef X

  // Returns the process-wide instance, or nullptr if libdbus could not be
  // loaded.
  static DBusLib *get();

private:
  void *mHandle = nullptr;
  bool load();
};
#endif
