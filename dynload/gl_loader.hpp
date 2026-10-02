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
  X(glBlendFuncSeparate)                                                       \
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
  X(glViewport)                                                                \
  X(glLoadIdentity)                                                            \
  X(glOrtho)                                                                   \
  X(glMatrixMode)                                                              \
  X(glLineWidth)                                                               \
  X(glPointSize)                                                               \
  X(glReadPixels)

struct GLLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_GL_FUNCS(X)
#undef X

  void *handle = nullptr;
};

extern GLLib *GL_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define glAttachShader GL_LIB->glAttachShader
#define glBegin GL_LIB->glBegin
#define glBindAttribLocation GL_LIB->glBindAttribLocation
#define glBindTexture GL_LIB->glBindTexture
#define glBlendFunc GL_LIB->glBlendFunc
#define glBlendFuncSeparate GL_LIB->glBlendFuncSeparate
#define glClear GL_LIB->glClear
#define glClearColor GL_LIB->glClearColor
#define glColor3f GL_LIB->glColor3f
#define glColor4f GL_LIB->glColor4f
#define glCompileShader GL_LIB->glCompileShader
#define glCreateProgram GL_LIB->glCreateProgram
#define glCreateShader GL_LIB->glCreateShader
#define glDeleteShader GL_LIB->glDeleteShader
#define glDeleteTextures GL_LIB->glDeleteTextures
#define glDisable GL_LIB->glDisable
#define glEnable GL_LIB->glEnable
#define glEnd GL_LIB->glEnd
#define glGenTextures GL_LIB->glGenTextures
#define glGetProgramInfoLog GL_LIB->glGetProgramInfoLog
#define glGetProgramiv GL_LIB->glGetProgramiv
#define glGetShaderInfoLog GL_LIB->glGetShaderInfoLog
#define glGetShaderiv GL_LIB->glGetShaderiv
#define glGetUniformLocation GL_LIB->glGetUniformLocation
#define glLinkProgram GL_LIB->glLinkProgram
#define glPopAttrib GL_LIB->glPopAttrib
#define glPushAttrib GL_LIB->glPushAttrib
#define glShaderSource GL_LIB->glShaderSource
#define glTexCoord2f GL_LIB->glTexCoord2f
#define glTexImage2D GL_LIB->glTexImage2D
#define glTexParameteri GL_LIB->glTexParameteri
#define glUniform1i GL_LIB->glUniform1i
#define glUniform2f GL_LIB->glUniform2f
#define glUseProgram GL_LIB->glUseProgram
#define glVertex2f GL_LIB->glVertex2f
#define glVertex3f GL_LIB->glVertex3f
#define glViewport GL_LIB->glViewport
#define glLoadIdentity GL_LIB->glLoadIdentity
#define glOrtho GL_LIB->glOrtho
#define glMatrixMode GL_LIB->glMatrixMode
#define glLineWidth GL_LIB->glLineWidth
#define glPointSize GL_LIB->glPointSize
#define glReadPixels GL_LIB->glReadPixels

#endif
