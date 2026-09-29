#pragma once

#include "gl_loader.hpp"

/*
 * Utility functions that handle OpenGL texture IDs for us.
 */
class TextureManager {
public:
  static inline int NewGLTextureID(int w, int h, const GLvoid *rgba,
                                   bool antialiased = true) {
    GLuint texture = 0;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    antialiased ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    antialiased ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba);
    glBindTexture(GL_TEXTURE_2D, 0);

    return texture;
  }
  static inline void FreeGLTextureID(GLuint id) { glDeleteTextures(1, &id); }
};
