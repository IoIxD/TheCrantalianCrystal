#ifdef TCC_HAS_DBUS
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

  void *handle = nullptr;
};

extern DBusLib *DBUS_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define dbus_error_init DBUS_LIB->dbus_error_init
#define dbus_error_free DBUS_LIB->dbus_error_free
#define dbus_error_is_set DBUS_LIB->dbus_error_is_set
#define dbus_bus_get_private DBUS_LIB->dbus_bus_get_private
#define dbus_bus_get_unique_name DBUS_LIB->dbus_bus_get_unique_name
#define dbus_bus_request_name DBUS_LIB->dbus_bus_request_name
#define dbus_bus_name_has_owner DBUS_LIB->dbus_bus_name_has_owner
#define dbus_bus_add_match DBUS_LIB->dbus_bus_add_match
#define dbus_connection_set_exit_on_disconnect                                 \
  DBUS_LIB->dbus_connection_set_exit_on_disconnect
#define dbus_connection_add_filter DBUS_LIB->dbus_connection_add_filter
#define dbus_connection_read_write DBUS_LIB->dbus_connection_read_write
#define dbus_connection_dispatch DBUS_LIB->dbus_connection_dispatch
#define dbus_connection_send DBUS_LIB->dbus_connection_send
#define dbus_connection_send_with_reply_and_block                              \
  DBUS_LIB->dbus_connection_send_with_reply_and_block
#define dbus_connection_flush DBUS_LIB->dbus_connection_flush
#define dbus_connection_get_unix_fd DBUS_LIB->dbus_connection_get_unix_fd
#define dbus_connection_close DBUS_LIB->dbus_connection_close
#define dbus_connection_unref DBUS_LIB->dbus_connection_unref
#define dbus_message_new_method_call DBUS_LIB->dbus_message_new_method_call
#define dbus_message_new_method_return DBUS_LIB->dbus_message_new_method_return
#define dbus_message_new_error DBUS_LIB->dbus_message_new_error
#define dbus_message_new_signal DBUS_LIB->dbus_message_new_signal
#define dbus_message_unref DBUS_LIB->dbus_message_unref
#define dbus_message_set_no_reply DBUS_LIB->dbus_message_set_no_reply
#define dbus_message_append_args DBUS_LIB->dbus_message_append_args
#define dbus_message_get_args DBUS_LIB->dbus_message_get_args
#define dbus_message_get_type DBUS_LIB->dbus_message_get_type
#define dbus_message_get_sender DBUS_LIB->dbus_message_get_sender
#define dbus_message_get_path DBUS_LIB->dbus_message_get_path
#define dbus_message_get_interface DBUS_LIB->dbus_message_get_interface
#define dbus_message_get_member DBUS_LIB->dbus_message_get_member
#define dbus_message_is_signal DBUS_LIB->dbus_message_is_signal
#define dbus_message_iter_init DBUS_LIB->dbus_message_iter_init
#define dbus_message_iter_init_append DBUS_LIB->dbus_message_iter_init_append
#define dbus_message_iter_open_container                                       \
  DBUS_LIB->dbus_message_iter_open_container
#define dbus_message_iter_close_container                                      \
  DBUS_LIB->dbus_message_iter_close_container
#define dbus_message_iter_append_basic DBUS_LIB->dbus_message_iter_append_basic
#define dbus_message_iter_get_arg_type DBUS_LIB->dbus_message_iter_get_arg_type
#define dbus_message_iter_recurse DBUS_LIB->dbus_message_iter_recurse
#define dbus_message_iter_get_basic DBUS_LIB->dbus_message_iter_get_basic
#define dbus_message_iter_next DBUS_LIB->dbus_message_iter_next
#define dbus_message_iter_get_fixed_array                                      \
  DBUS_LIB->dbus_message_iter_get_fixed_array
#define dbus_connection_send_with_reply                                        \
  DBUS_LIB->dbus_connection_send_with_reply
#define dbus_pending_call_set_notify DBUS_LIB->dbus_pending_call_set_notify
#define dbus_pending_call_steal_reply DBUS_LIB->dbus_pending_call_steal_reply
#define dbus_pending_call_cancel DBUS_LIB->dbus_pending_call_cancel
#define dbus_pending_call_unref DBUS_LIB->dbus_pending_call_unref
#endif
#endif
