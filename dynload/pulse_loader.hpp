#ifdef TCC_HAS_PULSE
#pragma once

#include <pulse/pulseaudio.h>

#define TCC_PULSE_FUNCS(X)                                                     \
  X(pa_mainloop_new)                                                           \
  X(pa_mainloop_free)                                                          \
  X(pa_mainloop_get_api)                                                       \
  X(pa_mainloop_iterate)                                                       \
  X(pa_context_new)                                                            \
  X(pa_context_unref)                                                          \
  X(pa_context_connect)                                                        \
  X(pa_context_disconnect)                                                     \
  X(pa_context_get_state)                                                      \
  X(pa_context_set_state_callback)                                             \
  X(pa_context_set_subscribe_callback)                                         \
  X(pa_context_subscribe)                                                      \
  X(pa_context_get_server_info)                                                \
  X(pa_context_get_sink_info_list)                                             \
  X(pa_context_set_sink_volume_by_name)                                        \
  X(pa_context_set_sink_mute_by_name)                                          \
  X(pa_context_set_default_sink)                                               \
  X(pa_context_get_source_info_list)                                           \
  X(pa_context_set_source_volume_by_name)                                      \
  X(pa_context_set_source_mute_by_name)                                        \
  X(pa_context_set_default_source)                                             \
  X(pa_context_get_source_output_info_list)                                    \
  X(pa_context_get_sink_input_info_list)                                       \
  X(pa_context_set_sink_input_volume)                                          \
  X(pa_context_set_sink_input_mute)                                            \
  X(pa_proplist_gets)                                                          \
  X(pa_operation_unref)                                                        \
  X(pa_cvolume_avg)                                                            \
  X(pa_cvolume_scale)

// Function table for a dynamically loaded libpulse. Each member has the same
// name and signature as the libpulse function it points to.
struct PulseLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_PULSE_FUNCS(X)
#undef X
  void *handle = nullptr;
};

extern PulseLib *PL_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES

#define pa_mainloop_new PL_LIB->pa_mainloop_new
#define pa_mainloop_free PL_LIB->pa_mainloop_free
#define pa_mainloop_get_api PL_LIB->pa_mainloop_get_api
#define pa_mainloop_iterate PL_LIB->pa_mainloop_iterate
#define pa_context_new PL_LIB->pa_context_new
#define pa_context_unref PL_LIB->pa_context_unref
#define pa_context_connect PL_LIB->pa_context_connect
#define pa_context_disconnect PL_LIB->pa_context_disconnect
#define pa_context_get_state PL_LIB->pa_context_get_state
#define pa_context_set_state_callback PL_LIB->pa_context_set_state_callback
#define pa_context_set_subscribe_callback                                      \
  PL_LIB->pa_context_set_subscribe_callback
#define pa_context_subscribe PL_LIB->pa_context_subscribe
#define pa_context_get_server_info PL_LIB->pa_context_get_server_info
#define pa_context_get_sink_info_list PL_LIB->pa_context_get_sink_info_list
#define pa_context_set_sink_volume_by_name                                     \
  PL_LIB->pa_context_set_sink_volume_by_name
#define pa_context_set_sink_mute_by_name                                       \
  PL_LIB->pa_context_set_sink_mute_by_name
#define pa_context_set_default_sink PL_LIB->pa_context_set_default_sink
#define pa_context_get_source_info_list PL_LIB->pa_context_get_source_info_list
#define pa_context_set_source_volume_by_name                                   \
  PL_LIB->pa_context_set_source_volume_by_name
#define pa_context_set_source_mute_by_name                                     \
  PL_LIB->pa_context_set_source_mute_by_name
#define pa_context_set_default_source PL_LIB->pa_context_set_default_source
#define pa_context_get_source_output_info_list                                 \
  PL_LIB->pa_context_get_source_output_info_list
#define pa_context_get_sink_input_info_list                                    \
  PL_LIB->pa_context_get_sink_input_info_list
#define pa_context_set_sink_input_volume                                       \
  PL_LIB->pa_context_set_sink_input_volume
#define pa_context_set_sink_input_mute PL_LIB->pa_context_set_sink_input_mute
#define pa_proplist_gets PL_LIB->pa_proplist_gets
#define pa_operation_unref PL_LIB->pa_operation_unref
#define pa_cvolume_avg PL_LIB->pa_cvolume_avg
#define pa_cvolume_scale PL_LIB->pa_cvolume_scale

#endif
#endif
