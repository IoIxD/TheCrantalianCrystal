#ifdef TCC_SYSTRAY_PULSE
#pragma once

// Only the libpulse headers are used at compile time (for types, constants and
// function signatures); the library itself is loaded at runtime with dlopen.
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

  // Returns the process-wide instance, or nullptr if libpulse could not be
  // loaded.
  static PulseLib *get();

private:
  void *mHandle = nullptr;
  bool load();
};
#endif
