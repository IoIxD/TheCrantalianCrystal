#pragma once

#include <string>

/*
 * Loaded screensaver module.
 *
 * Expected imports:
 *      void *tcc_scr_init(); // returns the ctx that's passed to future
 * functions
 *
 *      void tcc_scr_draw(void *ctx, int width, int height);
 *
 *      void tcc_scr_free(void *ctx);
 *
 * draw and free are called with a GL context current, so the module can make
 * (and later free) GL objects in them.
 */
struct Screensaver {
  void *lib = nullptr;
  void *(*init)() = nullptr;
  void (*draw)(void *ctx, int width, int height) = nullptr;
  void (*free)(void *ctx) = nullptr;
  void *ctx = nullptr;

  Screensaver(std::string name);
  ~Screensaver();
};
