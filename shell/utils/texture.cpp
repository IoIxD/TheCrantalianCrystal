#include "texture.hpp"
#include <cstdio>

int TextureManager::NewGLTextureID(int w, int h, const GLvoid *rgba) {
  GLuint texture = 0;

  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba);
  glBindTexture(GL_TEXTURE_2D, 0);

  return texture;
}
void TextureManager::FreeGLTextureID(GLuint id) { glDeleteTextures(1, &id); }
