#include "texture.hpp"
#include <cstdio>

int TextureManager::NewGLTextureID(int w, int h, const GLvoid *rgba,
                                   bool antialiased) {
  auto gl = GLLib::get();
  GLuint texture = 0;

  gl->glGenTextures(1, &texture);
  gl->glBindTexture(GL_TEXTURE_2D, texture);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      antialiased ? GL_LINEAR : GL_NEAREST);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                      antialiased ? GL_LINEAR : GL_NEAREST);
  gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, rgba);
  gl->glBindTexture(GL_TEXTURE_2D, 0);

  return texture;
}
void TextureManager::FreeGLTextureID(GLuint id) {
  auto gl = GLLib::get();
  gl->glDeleteTextures(1, &id);
}
