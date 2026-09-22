#pragma once
#include <vector>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

class TextureManager {
  static std::vector<GLuint> KnownTextureIDs;
  static std::vector<GLuint> AvaliableTextureIDs;

public:
  static int NewGLTextureID(int w, int h, const GLvoid *rgba);

  static void FreeGLTextureID(GLuint id);
};
