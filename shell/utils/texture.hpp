#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

/*
 * Utility functions that handle OpenGL texture IDs for us.
 */
class TextureManager {
public:
  static int NewGLTextureID(int w, int h, const GLvoid *rgba);
  static void FreeGLTextureID(GLuint id);
};
