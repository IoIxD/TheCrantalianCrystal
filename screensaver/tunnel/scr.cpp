#include "dynload.hpp"
#include "gl_loader.hpp"
#include <cstdio>

#define EXPORT extern "C" __attribute__((visibility("default")))

EXPORT void tcc_scr_init() { dynload_setup::gl(); };
EXPORT void tcc_scr_draw(void *ctx) {
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
};
EXPORT void tcc_scr_free(void *ctx) { printf("FREE\n"); };
