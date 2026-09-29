#include "../utils/texture.hpp"
#include "../utils/utf8.hpp"
#include "client.hpp"
#include "ssd_shader.h"
#include <assert.h>
#include <csignal>
#include <format>

static GLuint create_shader_program() {
  auto gl = GLLib::get();
  GLuint vertex_shader = 0, fragment_shader = 0;

  int i = 0;
  for (auto shader : {&vertex_shader, &fragment_shader}) {
    *shader =
        gl->glCreateShader((i == 0) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    auto src = ((i == 0) ? SSD_VERT_SOURCE : SSD_FRAG_SOURCE);

    gl->glShaderSource(*shader, 1, &src, NULL);
    gl->glCompileShader(*shader);

    GLint status;
    gl->glGetShaderiv(*shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
      char log[512];
      gl->glGetShaderInfoLog(*shader, sizeof(log), NULL, log);
      printf("ERROR: shader compilation failed: %s\n", log);
      raise(SIGTRAP);
    }

    i++;
  };

  GLuint program = gl->glCreateProgram();
  gl->glAttachShader(program, vertex_shader);
  gl->glAttachShader(program, fragment_shader);
  gl->glBindAttribLocation(program, 0, "position");
  gl->glLinkProgram(program);

  GLint status;
  gl->glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    char log[512];
    gl->glGetProgramInfoLog(program, sizeof(log), NULL, log);
    printf("ERROR: shader program linking failed: %s\n", log);
    raise(SIGTRAP);
  }

  gl->glDeleteShader(vertex_shader);
  gl->glDeleteShader(fragment_shader);

  return program;
}

void TCCClient::Window::setup_decor() {
  auto egl = EGLLib::get();
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

  mEGLDisplay = egl->eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR,
                                           client->mDisplay, NULL);

  ret = egl->eglInitialize(mEGLDisplay, &major, &minor);
  assert(ret == EGL_TRUE);

  if (!egl->eglGetConfigs(mEGLDisplay, NULL, 0, &count) || count < 1)
    assert(0);

  configs = (EGLConfig *)calloc(count, sizeof *configs);
  assert(configs);

  ret = egl->eglChooseConfig(mEGLDisplay, config_attribs, configs, count, &n);
  assert(ret && n >= 1);

  mEGLConfig = configs[0];

  free(configs);

  mEGLWindow =
      egl->wl_egl_window_create(decor_surface, decor_width, decor_height);
  if (!mEGLWindow) {
    printf("ERROR: eglCreateWindowSurface, %0X\n", egl->eglGetError());
    has_decor = false;
    return;
  }

  ret = egl->eglBindAPI(EGL_OPENGL_API);
  assert(ret == EGL_TRUE);
  mEGLContext = egl->eglCreateContext(mEGLDisplay, mEGLConfig, EGL_NO_CONTEXT,
                                      contextAttribs);
  assert(mEGLContext);

  mEGLSurface = egl->eglCreatePlatformWindowSurface(mEGLDisplay, mEGLConfig,
                                                    mEGLWindow, NULL);
  if (mEGLSurface == EGL_NO_SURFACE) {
    printf("eglCreatePlatformWindowSurface error: %0X\n", egl->eglGetError());
    has_decor = false;
    return;
  }

  if (!egl->eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface,
                           mEGLContext)) {
    printf("eglMakeCurrent error (init) %08X\n", egl->eglGetError());
    has_decor = false;
    return;
    // raise(SIGTRAP);
  };

  egl->eglSwapInterval(mEGLDisplay, 0);

  mEGLShaderProgram = create_shader_program();

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
  auto egl = EGLLib::get();
  auto gl = GLLib::get();
  egl->wl_egl_window_resize(mEGLWindow, decor_width, decor_height, 0, 0);

  if (!egl->eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface,
                           mEGLContext)) {
    printf("eglMakeCurrent error %08X\n", egl->eglGetError());
    has_decor = false;
    return;
  };

  decor_draw_backing();
  decor_draw_icon();

  /* draw text */
  gl->glViewport(0, 0, decor_width, decor_height);
  mGlyphManager.draw_text(title, 32, 22, decor_width, decor_height, true,
                          false);

  egl->eglSwapBuffers(mEGLDisplay, mEGLSurface);
};

void TCCClient::Window::decor_draw_backing() {
  auto gl = GLLib::get();
  gl->glViewport(0, 0, decor_width, decor_height);

  gl->glClearColor(0.f, 0.0f, 0.f, 0.f);
  gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gl->glEnable(GL_BLEND);
  gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  gl->glUseProgram(mEGLShaderProgram);
  gl->glUniform2f(gl->glGetUniformLocation(mEGLShaderProgram, "resolution"),
                  (float)decor_width, (float)decor_height);

  gl->glUniform1i(
      gl->glGetUniformLocation(mEGLShaderProgram, "ssd_border_size"),
      SSD_BORDER_SIZE);
  gl->glUniform1i(
      gl->glGetUniformLocation(mEGLShaderProgram, "ssd_border_size_top"),
      SSD_BORDER_SIZE_TOP - 1);

  gl->glUniform1i(gl->glGetUniformLocation(mEGLShaderProgram, "close_held"),
                  close_held);
  gl->glUniform1i(gl->glGetUniformLocation(mEGLShaderProgram, "minimize_held"),
                  minimize_held);
  gl->glUniform1i(gl->glGetUniformLocation(mEGLShaderProgram, "maximize_held"),
                  maximize_held);
  gl->glUniform1i(gl->glGetUniformLocation(mEGLShaderProgram, "close_hover"),
                  close_hover);
  gl->glUniform1i(gl->glGetUniformLocation(mEGLShaderProgram, "minimize_hover"),
                  minimize_hover);
  gl->glUniform1i(gl->glGetUniformLocation(mEGLShaderProgram, "maximize_hover"),
                  maximize_hover);
  gl->glUniform1i(gl->glGetUniformLocation(mEGLShaderProgram, "show_maximize"),
                  show_maximize);
  gl->glBegin(GL_QUADS);
  gl->glTexCoord2f(0.0f, 1.0f);
  gl->glVertex3f(-1, -1, 1); // bottom-left
  gl->glTexCoord2f(1.0f, 1.0f);
  gl->glVertex3f(1, -1, 1);
  gl->glTexCoord2f(1.0f, 0.0f);
  gl->glVertex3f(1, 1, 1);
  gl->glTexCoord2f(0.0f, 0.0f);
  gl->glVertex3f(-1, 1, 1); // top-right
  gl->glEnd();

  gl->glUseProgram(0);
  gl->glDisable(GL_BLEND);
}
void TCCClient::Window::decor_draw_icon() {
  auto gl = GLLib::get();
  if (decor_icon_texture != -1) {
    gl->glViewport(10, decor_height - 20 - SSD_BORDER_LEEWAY, 16, 16);

    gl->glEnable(GL_TEXTURE_2D);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl->glBindTexture(GL_TEXTURE_2D, decor_icon_texture);
    gl->glBegin(GL_QUADS);
    gl->glTexCoord2f(0.0f, 1.0f);
    gl->glVertex3f(-1, -1, 1); // bottom-left
    gl->glTexCoord2f(1.0f, 1.0f);
    gl->glVertex3f(1, -1, 1);
    gl->glTexCoord2f(1.0f, 0.0f);
    gl->glVertex3f(1, 1, 1);
    gl->glTexCoord2f(0.0f, 0.0f);
    gl->glVertex3f(-1, 1, 1); // top-right
    gl->glEnd();
    gl->glBindTexture(GL_TEXTURE_2D, 0);

    gl->glDisable(GL_BLEND);
  }
}

TCCClient::Window::~Window() {
  auto egl = EGLLib::get();
  if (!egl->eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface,
                           mEGLContext)) {
    printf("eglMakeCurrent error %08X\n", egl->eglGetError());
    has_decor = false;
    return;
  };

  for (auto glyph : mGlyphManager.glyphs()) {
    glyph->destroy();
  }

  egl->wl_egl_window_destroy(mEGLWindow);
  egl->eglDestroyContext(mEGLDisplay, mEGLContext);
  egl->eglDestroyContext(mEGLDisplay, mEGLSurface);
}
