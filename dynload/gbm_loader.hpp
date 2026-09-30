#pragma once

#include <gbm.h>

#define TCC_GBM_FUNCS(X)                                                       \
  X(gbm_create_device)                                                         \
  X(gbm_device_destroy)                                                        \
  X(gbm_device_get_format_modifier_plane_count)                                \
  X(gbm_bo_create)                                                             \
  X(gbm_bo_create_with_modifiers)                                              \
  X(gbm_bo_destroy)                                                            \
  X(gbm_bo_get_fd)                                                             \
  X(gbm_bo_get_modifier)                                                       \
  X(gbm_bo_get_offset)                                                         \
  X(gbm_bo_get_plane_count)                                                    \
  X(gbm_bo_get_stride)

struct GbmLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_GBM_FUNCS(X)
#undef X

  void *handle = nullptr;
};

extern GbmLib *GBM_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define gbm_create_device GBM_LIB->gbm_create_device
#define gbm_device_destroy GBM_LIB->gbm_device_destroy
#define gbm_device_get_format_modifier_plane_count                             \
  GBM_LIB->gbm_device_get_format_modifier_plane_count
#define gbm_bo_create GBM_LIB->gbm_bo_create
#define gbm_bo_create_with_modifiers GBM_LIB->gbm_bo_create_with_modifiers
#define gbm_bo_destroy GBM_LIB->gbm_bo_destroy
#define gbm_bo_get_fd GBM_LIB->gbm_bo_get_fd
#define gbm_bo_get_modifier GBM_LIB->gbm_bo_get_modifier
#define gbm_bo_get_offset GBM_LIB->gbm_bo_get_offset
#define gbm_bo_get_plane_count GBM_LIB->gbm_bo_get_plane_count
#define gbm_bo_get_stride GBM_LIB->gbm_bo_get_stride
#endif
