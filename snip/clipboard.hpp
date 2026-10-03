#pragma once

#include <vector>

// Encodes an RGBA image (top row first) as an uncompressed PNG, without its
// alpha.
std::vector<unsigned char> encode_png(const unsigned char *rgba, int width,
                                      int height);

/*
 * Puts the PNG on the clipboard, with ext-data-control. Something has to stay
 * around to hand it to whoever pastes it, so this forks off a process that
 * does until something else takes the clipboard. Returns whether it got that
 * far.
 */
bool copy_png_to_clipboard(std::vector<unsigned char> png);
