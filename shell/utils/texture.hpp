#pragma once
#include <vector>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

/*
 * Class that handles OpenGL texture IDs for us.
 * In addition to setting appropriate defaults for us it also handles reusage of
 * old texture IDs.
 *
 * That last part sounds like something EGL should be doing, doesn't it? So why
 * is it then when I use glDeleteTextures in a loop (which is done for glyphs)
 * every texture I've ever created gets unloaded and the desktop background
 * turns to white? Yeah I don't know, this kind of defies what I know about
 * OpenGL. But it works, and I need functions like this for automating shit
 * anyways, so... yeah, sure, fuck it.
 */
class TextureManager {
  static std::vector<GLuint> KnownTextureIDs;
  static std::vector<GLuint> AvaliableTextureIDs;

public:
  static int NewGLTextureID(int w, int h, const GLvoid *rgba);
  static void FreeGLTextureID(GLuint id);
};
