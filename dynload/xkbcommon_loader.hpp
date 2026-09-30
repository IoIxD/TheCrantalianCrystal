#pragma once

#include <xkbcommon/xkbcommon.h>

#define TCC_XKBCOMMON_FUNCS(X)                                                 \
  X(xkb_context_new)                                                           \
  X(xkb_context_unref)                                                         \
  X(xkb_keymap_new_from_string)                                                \
  X(xkb_keymap_unref)                                                          \
  X(xkb_state_new)                                                             \
  X(xkb_state_unref)                                                           \
  X(xkb_state_update_mask)                                                     \
  X(xkb_state_key_get_one_sym)                                                 \
  X(xkb_state_key_get_utf8)

struct XkbcommonLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_XKBCOMMON_FUNCS(X)
#undef X

  void *handle = nullptr;
};

extern XkbcommonLib *XKB_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define xkb_context_new XKB_LIB->xkb_context_new
#define xkb_context_unref XKB_LIB->xkb_context_unref
#define xkb_keymap_new_from_string XKB_LIB->xkb_keymap_new_from_string
#define xkb_keymap_unref XKB_LIB->xkb_keymap_unref
#define xkb_state_new XKB_LIB->xkb_state_new
#define xkb_state_unref XKB_LIB->xkb_state_unref
#define xkb_state_update_mask XKB_LIB->xkb_state_update_mask
#define xkb_state_key_get_one_sym XKB_LIB->xkb_state_key_get_one_sym
#define xkb_state_key_get_utf8 XKB_LIB->xkb_state_key_get_utf8
#endif
