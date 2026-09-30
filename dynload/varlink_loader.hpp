#pragma once

#include <varlink.h>

#define TCC_VARLINK_FUNCS(X)                                                   \
  X(varlink_object_new);                                                       \
  X(varlink_object_new_from_json);                                             \
  X(varlink_object_unref);                                                     \
  X(varlink_object_unrefp);                                                    \
  X(varlink_object_ref);                                                       \
  X(varlink_object_to_json);                                                   \
  X(varlink_object_get_field_names);                                           \
  X(varlink_object_set_null);                                                  \
  X(varlink_object_get_bool);                                                  \
  X(varlink_object_get_int);                                                   \
  X(varlink_object_get_float);                                                 \
  X(varlink_object_get_string);                                                \
  X(varlink_object_get_array);                                                 \
  X(varlink_object_get_object);                                                \
  X(varlink_object_set_bool);                                                  \
  X(varlink_object_set_int);                                                   \
  X(varlink_object_set_float);                                                 \
  X(varlink_object_set_string);                                                \
  X(varlink_object_set_array);                                                 \
  X(varlink_object_set_object);                                                \
  X(varlink_array_new);                                                        \
  X(varlink_array_ref);                                                        \
  X(varlink_array_unref);                                                      \
  X(varlink_array_unrefp);                                                     \
  X(varlink_array_get_n_elements);                                             \
  X(varlink_array_get_bool);                                                   \
  X(varlink_array_get_int);                                                    \
  X(varlink_array_get_float);                                                  \
  X(varlink_array_get_string);                                                 \
  X(varlink_array_get_array);                                                  \
  X(varlink_array_get_object);                                                 \
  X(varlink_array_append_null);                                                \
  X(varlink_array_append_bool);                                                \
  X(varlink_array_append_int);                                                 \
  X(varlink_array_append_float);                                               \
  X(varlink_array_append_string);                                              \
  X(varlink_array_append_array);                                               \
  X(varlink_array_append_object);                                              \
  X(varlink_service_new);                                                      \
  X(varlink_service_new_raw);                                                  \
  X(varlink_service_free);                                                     \
  X(varlink_service_freep);                                                    \
  X(varlink_service_add_interface);                                            \
  X(varlink_service_get_fd);                                                   \
  X(varlink_listen);                                                           \
  X(varlink_service_process_events);                                           \
  X(varlink_call_ref);                                                         \
  X(varlink_call_unref);                                                       \
  X(varlink_call_unrefp);                                                      \
  X(varlink_call_get_method);                                                  \
  X(varlink_call_set_connection_closed_callback);                              \
  X(varlink_call_get_connection_userdata);                                     \
  X(varlink_call_get_connection_fd);                                           \
  X(varlink_call_reply);                                                       \
  X(varlink_call_reply_error);                                                 \
  X(varlink_call_reply_invalid_parameter);                                     \
  X(varlink_connection_new);                                                   \
  X(varlink_connection_free);                                                  \
  X(varlink_connection_freep);                                                 \
  X(varlink_connection_set_closed_callback);                                   \
  X(varlink_connection_get_userdata);                                          \
  X(varlink_connection_get_fd);                                                \
  X(varlink_connection_process_events);                                        \
  X(varlink_connection_get_events);                                            \
  X(varlink_connection_call);                                                  \
  X(varlink_connection_close);                                                 \
  X(varlink_connection_is_closed);                                             \
  X(varlink_error_string);

struct VarlinkLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_VARLINK_FUNCS(X)
#undef X
  void *handle = nullptr;
};

extern VarlinkLib *VARLINK_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define varlink_object_new VARLINK_LIB->varlink_object_new
#define varlink_object_new_from_json VARLINK_LIB->varlink_object_new_from_json
#define varlink_object_unref VARLINK_LIB->varlink_object_unref
#define varlink_object_unrefp VARLINK_LIB->varlink_object_unrefp
#define varlink_object_ref VARLINK_LIB->varlink_object_ref
#define varlink_object_to_json VARLINK_LIB->varlink_object_to_json
#define varlink_object_get_field_names                                         \
  VARLINK_LIB->varlink_object_get_field_names
#define varlink_object_set_null VARLINK_LIB->varlink_object_set_null
#define varlink_object_get_bool VARLINK_LIB->varlink_object_get_bool
#define varlink_object_get_int VARLINK_LIB->varlink_object_get_int
#define varlink_object_get_float VARLINK_LIB->varlink_object_get_float
#define varlink_object_get_string VARLINK_LIB->varlink_object_get_string
#define varlink_object_get_array VARLINK_LIB->varlink_object_get_array
#define varlink_object_get_object VARLINK_LIB->varlink_object_get_object
#define varlink_object_set_bool VARLINK_LIB->varlink_object_set_bool
#define varlink_object_set_int VARLINK_LIB->varlink_object_set_int
#define varlink_object_set_float VARLINK_LIB->varlink_object_set_float
#define varlink_object_set_string VARLINK_LIB->varlink_object_set_string
#define varlink_object_set_array VARLINK_LIB->varlink_object_set_array
#define varlink_object_set_object VARLINK_LIB->varlink_object_set_object
#define varlink_array_new VARLINK_LIB->varlink_array_new
#define varlink_array_ref VARLINK_LIB->varlink_array_ref
#define varlink_array_unref VARLINK_LIB->varlink_array_unref
#define varlink_array_unrefp VARLINK_LIB->varlink_array_unrefp
#define varlink_array_get_n_elements VARLINK_LIB->varlink_array_get_n_elements
#define varlink_array_get_bool VARLINK_LIB->varlink_array_get_bool
#define varlink_array_get_int VARLINK_LIB->varlink_array_get_int
#define varlink_array_get_float VARLINK_LIB->varlink_array_get_float
#define varlink_array_get_string VARLINK_LIB->varlink_array_get_string
#define varlink_array_get_array VARLINK_LIB->varlink_array_get_array
#define varlink_array_get_object VARLINK_LIB->varlink_array_get_object
#define varlink_array_append_null VARLINK_LIB->varlink_array_append_null
#define varlink_array_append_bool VARLINK_LIB->varlink_array_append_bool
#define varlink_array_append_int VARLINK_LIB->varlink_array_append_int
#define varlink_array_append_float VARLINK_LIB->varlink_array_append_float
#define varlink_array_append_string VARLINK_LIB->varlink_array_append_string
#define varlink_array_append_array VARLINK_LIB->varlink_array_append_array
#define varlink_array_append_object VARLINK_LIB->varlink_array_append_object
#define varlink_service_new VARLINK_LIB->varlink_service_new
#define varlink_service_new_raw VARLINK_LIB->varlink_service_new_raw
#define varlink_service_free VARLINK_LIB->varlink_service_free
#define varlink_service_freep VARLINK_LIB->varlink_service_freep
#define varlink_service_add_interface VARLINK_LIB->varlink_service_add_interface
#define varlink_service_get_fd VARLINK_LIB->varlink_service_get_fd
#define varlink_listen VARLINK_LIB->varlink_listen
#define varlink_service_process_events                                         \
  VARLINK_LIB->varlink_service_process_events
#define varlink_call_ref VARLINK_LIB->varlink_call_ref
#define varlink_call_unref VARLINK_LIB->varlink_call_unref
#define varlink_call_unrefp VARLINK_LIB->varlink_call_unrefp
#define varlink_call_get_method VARLINK_LIB->varlink_call_get_method
#define varlink_call_set_connection_closed_callback                            \
  VARLINK_LIB->varlink_call_set_connection_closed_callback
#define varlink_call_get_connection_userdata                                   \
  VARLINK_LIB->varlink_call_get_connection_userdata
#define varlink_call_get_connection_fd                                         \
  VARLINK_LIB->varlink_call_get_connection_fd
#define varlink_call_reply VARLINK_LIB->varlink_call_reply
#define varlink_call_reply_error VARLINK_LIB->varlink_call_reply_error
#define varlink_call_reply_invalid_parameter                                   \
  VARLINK_LIB->varlink_call_reply_invalid_parameter
#define varlink_connection_new VARLINK_LIB->varlink_connection_new
#define varlink_connection_free VARLINK_LIB->varlink_connection_free
#define varlink_connection_freep VARLINK_LIB->varlink_connection_freep
#define varlink_connection_set_closed_callback                                 \
  VARLINK_LIB->varlink_connection_set_closed_callback
#define varlink_connection_get_userdata                                        \
  VARLINK_LIB->varlink_connection_get_userdata
#define varlink_connection_get_fd VARLINK_LIB->varlink_connection_get_fd
#define varlink_connection_process_events                                      \
  VARLINK_LIB->varlink_connection_process_events
#define varlink_connection_get_events VARLINK_LIB->varlink_connection_get_events
#define varlink_connection_call VARLINK_LIB->varlink_connection_call
#define varlink_connection_close VARLINK_LIB->varlink_connection_close
#define varlink_connection_is_closed VARLINK_LIB->varlink_connection_is_closed
#define varlink_error_string VARLINK_LIB->varlink_error_string
#endif
