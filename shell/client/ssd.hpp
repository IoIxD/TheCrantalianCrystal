#pragma once

#include "gl_loader.hpp"

// Where the title goes in the decoration, from its top left. Leaves room for
// the icon.
#define SSD_TITLE_X 32
#define SSD_TITLE_Y 22
// How round the decoration's corners are (the 7.0 in ssd.frag).
#define SSD_CORNER_RADIUS 7

// The nav button state the decoration shader draws.
struct SSDNavButtons {
  bool close_held = false;
  bool minimize_held = false;
  bool maximize_held = false;
  bool close_hover = false;
  bool minimize_hover = false;
  bool maximize_hover = false;
  bool show_maximize = true;
};

// With ssd.vert, and fragment_source either ssd.frag or a variation of it.
GLuint ssd_create_shader_program(const char *fragment_source);

/*
 * Draws a decoration (frame and title bar) filling the current viewport,
 * which should be width x height.
 */
void ssd_draw_backing(GLuint program, int width, int height,
                      const SSDNavButtons &buttons);

/*
 * Draws the title bar icon of a decoration whose bottom left corner is at
 * (x, y) in GL coordinates, and that's height tall. Leaves the viewport
 * changed.
 */
void ssd_draw_icon(GLuint texture, int x, int y, int height);
