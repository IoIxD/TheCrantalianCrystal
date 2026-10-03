#pragma once

#include <cstdint>
#include <vector>

// A copy of part of an output, from before the clock was put over it.
struct Backdrop {
  // RGBA, top row first, at the output's own resolution.
  int width = 0, height = 0;
  std::vector<unsigned char> pixels;
  // The whole output's size, in logical pixels.
  int output_width = 0, output_height = 0;
};

/*
 * Copies the area (x, y, width, height) of an output, in logical coordinates
 * from its top left, with ext-image-copy-capture. output_name is the output's
 * wl_output global, or 0 for whichever comes first. Anything past the edge of
 * the output is filled in from the nearest pixel on it. A width or height of 0
 * copies the whole output.
 */
bool capture_backdrop(uint32_t output_name, int x, int y, int width,
                      int height, Backdrop *out);
