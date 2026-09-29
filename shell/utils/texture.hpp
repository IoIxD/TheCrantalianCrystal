#pragma once

#include "../lib/gl_loader.hpp"

/*
 * Utility functions that handle OpenGL texture IDs for us.
 */
class TextureManager {
public:
  static int NewGLTextureID(int w, int h, const GLvoid *rgba,
                            bool antialiased = true);
  static void FreeGLTextureID(GLuint id);
};
