#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#define TCC_GL_FUNCS(X)                                                        \
  X(glAttachShader)                                                            \
  X(glBegin)                                                                   \
  X(glBindAttribLocation)                                                      \
  X(glBindTexture)                                                             \
  X(glBlendFunc)                                                               \
  X(glClear)                                                                   \
  X(glClearColor)                                                              \
  X(glColor3f)                                                                 \
  X(glColor4f)                                                                 \
  X(glCompileShader)                                                           \
  X(glCreateProgram)                                                           \
  X(glCreateShader)                                                            \
  X(glDeleteShader)                                                            \
  X(glDeleteTextures)                                                          \
  X(glDisable)                                                                 \
  X(glEnable)                                                                  \
  X(glEnd)                                                                     \
  X(glGenTextures)                                                             \
  X(glGetProgramInfoLog)                                                       \
  X(glGetProgramiv)                                                            \
  X(glGetShaderInfoLog)                                                        \
  X(glGetShaderiv)                                                             \
  X(glGetUniformLocation)                                                      \
  X(glLinkProgram)                                                             \
  X(glPopAttrib)                                                               \
  X(glPushAttrib)                                                              \
  X(glShaderSource)                                                            \
  X(glTexCoord2f)                                                              \
  X(glTexImage2D)                                                              \
  X(glTexParameteri)                                                           \
  X(glUniform1i)                                                               \
  X(glUniform2f)                                                               \
  X(glUseProgram)                                                              \
  X(glVertex2f)                                                                \
  X(glVertex3f)                                                                \
  X(glViewport)

struct GLLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_GL_FUNCS(X)
#undef X

  // Returns the process-wide instance, or nullptr if libGL could not be loaded.
  static GLLib *get();

private:
  void *mHandle = nullptr;
  bool load();
};
