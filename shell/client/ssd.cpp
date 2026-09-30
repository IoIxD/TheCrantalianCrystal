#include "../utils/texture.hpp"
#include "../utils/utf8.hpp"
#include "ssd.hpp"
#include "client.hpp"
#include "ssd_shader.h"
#include <assert.h>
#include <csignal>
#include <format>

GLuint ssd_create_shader_program(const char *fragment_source) {
  GLuint vertex_shader = 0, fragment_shader = 0;

  int i = 0;
  for (auto shader : {&vertex_shader, &fragment_shader}) {
    *shader = glCreateShader((i == 0) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    auto src = ((i == 0) ? SSD_VERT_SOURCE : fragment_source);

    glShaderSource(*shader, 1, &src, NULL);
    glCompileShader(*shader);

    GLint status;
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
      char log[512];
      glGetShaderInfoLog(*shader, sizeof(log), NULL, log);
      printf("ERROR: shader compilation failed: %s\n", log);
      raise(SIGTRAP);
    }

    i++;
  };

  GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glBindAttribLocation(program, 0, "position");
  glLinkProgram(program);

  GLint status;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    char log[512];
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    printf("ERROR: shader program linking failed: %s\n", log);
    raise(SIGTRAP);
  }

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  return program;
}

void TCCClient::Window::setup_decor() {
  river_decoration_v1_set_offset(decor_decor, -SSD_BORDER_SIZE,
                                 -SSD_BORDER_SIZE_TOP);
  this->client->window_set_position(this, this->x + SSD_BORDER_SIZE,
                                    this->y + SSD_BORDER_SIZE_TOP);

  const char *extensions;

  EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                             EGL_WINDOW_BIT,
                             EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_BIT,
                             EGL_RED_SIZE,
                             8,
                             EGL_GREEN_SIZE,
                             8,
                             EGL_BLUE_SIZE,
                             8,
                             EGL_ALPHA_SIZE,
                             8,
                             EGL_DEPTH_SIZE,
                             24,
                             EGL_NONE};
  EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION,
                             1,
                             EGL_CONTEXT_MAJOR_VERSION,
                             1,
                             EGL_CONTEXT_MINOR_VERSION,
                             1,
                             EGL_NONE};
  EGLint major, minor, n, count, i, size;
  EGLConfig *configs;
  EGLBoolean ret;

  mEGLDisplay =
      eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, client->mDisplay, NULL);

  ret = eglInitialize(mEGLDisplay, &major, &minor);
  assert(ret == EGL_TRUE);

  if (!eglGetConfigs(mEGLDisplay, NULL, 0, &count) || count < 1)
    assert(0);

  configs = (EGLConfig *)calloc(count, sizeof *configs);
  assert(configs);

  ret = eglChooseConfig(mEGLDisplay, config_attribs, configs, count, &n);
  assert(ret && n >= 1);

  mEGLConfig = configs[0];

  free(configs);

  mEGLWindow = wl_egl_window_create(decor_surface, decor_width, decor_height);
  if (!mEGLWindow) {
    printf("ERROR: eglCreateWindowSurface, %0X\n", eglGetError());
    has_decor = false;
    return;
  }

  ret = eglBindAPI(EGL_OPENGL_API);
  assert(ret == EGL_TRUE);
  mEGLContext =
      eglCreateContext(mEGLDisplay, mEGLConfig, EGL_NO_CONTEXT, contextAttribs);
  assert(mEGLContext);

  mEGLSurface =
      eglCreatePlatformWindowSurface(mEGLDisplay, mEGLConfig, mEGLWindow, NULL);
  if (mEGLSurface == EGL_NO_SURFACE) {
    printf("eglCreatePlatformWindowSurface error: %0X\n", eglGetError());
    has_decor = false;
    return;
  }

  if (!eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext)) {
    printf("eglMakeCurrent error (init) %08X\n", eglGetError());
    has_decor = false;
    return;
    // raise(SIGTRAP);
  };

  eglSwapInterval(mEGLDisplay, 0);

  mEGLShaderProgram = ssd_create_shader_program(SSD_FRAG_SOURCE);

  int width = 0, height = 0;
  unsigned char *pixels = nullptr;
  client->mIconManager.get_icon(app_id, 16, &width, &height, &pixels);

  if (pixels) {
    decor_icon_texture = TextureManager::NewGLTextureID(width, height, pixels);
    printf("width %d height %d\n", width, height);
    printf("pixels %p\n", pixels);
    free(pixels);
  }
};

void TCCClient::Window::decor_draw() {
  if (!mEGLWindow) {
    printf("WARNING: no %p\n", mEGLWindow);
    return;
  }
  wl_egl_window_resize(mEGLWindow, decor_width, decor_height, 0, 0);

  if (!eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext)) {
    printf("eglMakeCurrent error %08X\n", eglGetError());
    has_decor = false;
    return;
  };

  decor_draw_backing();
  decor_draw_icon();

  /* draw text */
  glViewport(0, 0, decor_width, decor_height);
  mGlyphManager.draw_text(title, SSD_TITLE_X, SSD_TITLE_Y, decor_width,
                          decor_height, true, false);

  eglSwapBuffers(mEGLDisplay, mEGLSurface);
};

void TCCClient::Window::decor_draw_backing() {
  glViewport(0, 0, decor_width, decor_height);

  glClearColor(0.f, 0.0f, 0.f, 0.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  SSDNavButtons buttons;
  buttons.close_held = close_held;
  buttons.minimize_held = minimize_held;
  buttons.maximize_held = maximize_held;
  buttons.close_hover = close_hover;
  buttons.minimize_hover = minimize_hover;
  buttons.maximize_hover = maximize_hover;
  buttons.show_maximize = show_maximize;
  ssd_draw_backing(mEGLShaderProgram, decor_width, decor_height, buttons);
}

void ssd_draw_backing(GLuint program, int width, int height,
                      const SSDNavButtons &buttons) {
  glEnable(GL_BLEND);
  // (so partly see-through pixels don't come out even more see-through)
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(program);
  glUniform2f(glGetUniformLocation(program, "resolution"), (float)width,
              (float)height);

  glUniform1i(glGetUniformLocation(program, "ssd_border_size"),
              SSD_BORDER_SIZE);
  glUniform1i(glGetUniformLocation(program, "ssd_border_size_top"),
              SSD_BORDER_SIZE_TOP - 1);

  glUniform1i(glGetUniformLocation(program, "close_held"), buttons.close_held);
  glUniform1i(glGetUniformLocation(program, "minimize_held"),
              buttons.minimize_held);
  glUniform1i(glGetUniformLocation(program, "maximize_held"),
              buttons.maximize_held);
  glUniform1i(glGetUniformLocation(program, "close_hover"),
              buttons.close_hover);
  glUniform1i(glGetUniformLocation(program, "minimize_hover"),
              buttons.minimize_hover);
  glUniform1i(glGetUniformLocation(program, "maximize_hover"),
              buttons.maximize_hover);
  glUniform1i(glGetUniformLocation(program, "show_maximize"),
              buttons.show_maximize);
  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 1.0f);
  glVertex3f(-1, -1, 1); // bottom-left
  glTexCoord2f(1.0f, 1.0f);
  glVertex3f(1, -1, 1);
  glTexCoord2f(1.0f, 0.0f);
  glVertex3f(1, 1, 1);
  glTexCoord2f(0.0f, 0.0f);
  glVertex3f(-1, 1, 1); // top-right
  glEnd();

  glUseProgram(0);
  glDisable(GL_BLEND);
}
void TCCClient::Window::decor_draw_icon() {
  if (decor_icon_texture != -1) {
    ssd_draw_icon(decor_icon_texture, 0, 0, decor_height);
  }
}

void ssd_draw_icon(GLuint texture, int x, int y, int height) {
  glViewport(x + 10, y + height - 20 - SSD_BORDER_LEEWAY, 16, 16);

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  // (so partly see-through pixels don't come out even more see-through)
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);

  glBindTexture(GL_TEXTURE_2D, texture);
  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 1.0f);
  glVertex3f(-1, -1, 1); // bottom-left
  glTexCoord2f(1.0f, 1.0f);
  glVertex3f(1, -1, 1);
  glTexCoord2f(1.0f, 0.0f);
  glVertex3f(1, 1, 1);
  glTexCoord2f(0.0f, 0.0f);
  glVertex3f(-1, 1, 1); // top-right
  glEnd();
  glBindTexture(GL_TEXTURE_2D, 0);

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
}

TCCClient::Window::~Window() {
  if (!eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext)) {
    printf("eglMakeCurrent error %08X\n", eglGetError());
    has_decor = false;
    return;
  };

  for (auto glyph : mGlyphManager.glyphs()) {
    glyph->destroy();
  }

  wl_egl_window_destroy(mEGLWindow);
  eglDestroyContext(mEGLDisplay, mEGLContext);
  eglDestroyContext(mEGLDisplay, mEGLSurface);
}
