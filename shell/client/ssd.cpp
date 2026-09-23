#include "../utils/texture.hpp"
#include "client.hpp"
#include "ssd_shader.h"
#include <assert.h>
#include <csignal>
#include <format>

static GLuint create_shader_program() {
  GLuint vertex_shader = 0, fragment_shader = 0;

  int i = 0;
  for (auto shader : {&vertex_shader, &fragment_shader}) {
    *shader = glCreateShader((i == 0) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    auto src = ((i == 0) ? SSD_VERT_SOURCE : SSD_FRAG_SOURCE);

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

void TCCClient::Window::setup_egl() {
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

  egl_display =
      eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, client->mDisplay, NULL);

  ret = eglInitialize(egl_display, &major, &minor);
  assert(ret == EGL_TRUE);

  if (!eglGetConfigs(egl_display, NULL, 0, &count) || count < 1)
    assert(0);

  configs = (EGLConfig *)calloc(count, sizeof *configs);
  assert(configs);

  ret = eglChooseConfig(egl_display, config_attribs, configs, count, &n);
  assert(ret && n >= 1);

  egl_config = configs[0];

  free(configs);

  egl_window =
      wl_egl_window_create(main_decor.surface, decor_width, decor_height);
  if (!egl_window) {
    printf("ERROR: eglCreateWindowSurface, %0X\n", eglGetError());
    raise(SIGTRAP);
  }

  ret = eglBindAPI(EGL_OPENGL_API);
  assert(ret == EGL_TRUE);
  egl_context =
      eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, contextAttribs);
  assert(egl_context);

  egl_surface =
      eglCreatePlatformWindowSurface(egl_display, egl_config, egl_window, NULL);
  if (egl_surface == EGL_NO_SURFACE) {
    printf("eglCreatePlatformWindowSurface error: %0X\n", eglGetError());
    raise(SIGTRAP);
  }

  if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
    printf("eglMakeCurrent error (init) %08X\n", eglGetError());
    raise(SIGTRAP);
  };

  eglSwapInterval(egl_display, 0);

  egl_shader_program = create_shader_program();
};

void TCCClient::Window::egl_draw() {
  wl_egl_window_resize(egl_window, decor_width + SSD_BORDER_LEEWAY,
                       decor_height + SSD_BORDER_LEEWAY, 0, 0);

  if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
    printf("eglMakeCurrent error (init) %08X\n", eglGetError());
    raise(SIGTRAP);
  };

  glViewport(0, SSD_BORDER_LEEWAY, decor_width, decor_height);

  glClearColor(0.f, 0.0f, 0.f, 0.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(egl_shader_program);
  glUniform2f(glGetUniformLocation(egl_shader_program, "resolution"),
              (float)decor_width, (float)decor_height);

  glUniform1i(glGetUniformLocation(egl_shader_program, "ssd_border_size"),
              SSD_BORDER_SIZE);
  glUniform1i(glGetUniformLocation(egl_shader_program, "ssd_border_size_top"),
              SSD_BORDER_SIZE_TOP - 1);

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

  draw_text(title, 32, 22, true);

  eglSwapBuffers(egl_display, egl_surface);
};

// Loads and caches a glyph's texture the first time it's needed
std::shared_ptr<TCCClient::Window::Glyph>
TCCClient::Window::get_glyph(FT_Face f, unsigned long c) {
  FT_GlyphSlot slot = f->glyph;
  FT_Bitmap *bmp = &slot->bitmap;

  if (glyphCache.contains(c)) {
    return glyphCache.at(c);
  }

  auto g = std::make_shared<Glyph>();

  if (FT_Load_Char(f, c, FT_LOAD_RENDER)) {
    fprintf(stderr, "Failed to load glyph '%c'\n", c);
    return NULL;
  }

  g->width = bmp->width;
  g->height = bmp->rows;
  g->bearingX = slot->bitmap_left;
  g->bearingY = slot->bitmap_top;
  g->advance = slot->advance.x >> 6; // 26.6 fixed point -> pixels

  int w = g->width > 0 ? g->width : 1;
  int h = g->height > 0 ? g->height : 1;
  unsigned char *rgba = (unsigned char *)malloc(w * h * 4);
  for (int y = 0; y < g->height; y++) {
    for (int x = 0; x < g->width; x++) {
      unsigned char alpha = bmp->buffer[y * bmp->pitch + x];
      int idx = (y * w + x) * 4;
      rgba[idx + 0] = 255;
      rgba[idx + 1] = 255;
      rgba[idx + 2] = 255;
      rgba[idx + 3] = alpha;
    }
  }

  g->texture = TextureManager::NewGLTextureID(w, h, rgba);

  free(rgba);
  g->loaded = 1;

  glyphCache.insert_or_assign(c, g);
  return glyphCache.at(c);
}

void TCCClient::Window::draw_text(std::string text, int32_t x, int32_t y,
                                  bool bold) {
  glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);

  float penX = x;
  std::string text_ptr = text;
  const unsigned char *p = (const unsigned char *)text_ptr.c_str();

  while (*p) {
    /* extract a utf8 codepoint from the string. */
    uint32_t codepoint;
    if (p[0] < 0x80) {
      codepoint = p[0];
      p += 1;
    } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
      codepoint = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 &&
               (p[2] & 0xC0) == 0x80) {
      codepoint = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
    } else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
               (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
      codepoint = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                  ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
      p += 4;
    } else {
      // Invalid leading byte; consume it and use replacement char
      codepoint = 0xFFFD;
      p += 1;
    }

    auto g = get_glyph(bold ? client->mFTFaceBold : client->mFTFaceNormal,
                       codepoint);

    if (g->width > 0 && g->height > 0) {
      float px0 = penX + g->bearingX;
      // top of glyph
      float py0 = (float)y - g->bearingY;
      float x0 = (px0 / decor_width) * 2.0f - 1.0f;
      float y0 = 1.0f - (py0 / decor_height) * 2.0f;
      float x1 = ((float)(px0 + g->width) / decor_width) * 2.0f - 1.0f;
      // bottom of glyph
      float y1 = 1.0f - ((float)(py0 + g->height) / decor_height) * 2.0f;

      glBindTexture(GL_TEXTURE_2D, g->texture);
      glBegin(GL_QUADS);
      glTexCoord2f(0.0f, 0.0f);
      glVertex2f(x0, y0);
      glTexCoord2f(1.0f, 0.0f);
      glVertex2f(x1, y0);
      glTexCoord2f(1.0f, 1.0f);
      glVertex2f(x1, y1);
      glTexCoord2f(0.0f, 1.0f);
      glVertex2f(x0, y1);
      glEnd();
    }

    penX += g->advance;
  }

  glPopAttrib();
}
