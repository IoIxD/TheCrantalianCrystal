#include "clock.hpp"
#include "dynload.hpp"
#include "fonts.hpp"

#include "frost_shader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace clock_config;

// Room left around text in its texture, for glyphs that reach past where
// they're placed, in buffer pixels.
constexpr int TEXT_PADDING = 2;

static double deg2rad(double deg) { return (360.0 - deg) / 180.0 * M_PI; }

// Builds a program out of a fragment shader, or returns 0.
static GLuint build_program(const char *source) {
  GLint ok;
  GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "tcc_clock: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, shader);
  glLinkProgram(program);
  glDeleteShader(shader);
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    fprintf(stderr, "tcc_clock: %s\n", log);
    return 0;
  }
  return program;
}

// Makes a texture (left bound) that's smoothly scaled and doesn't wrap.
static GLuint create_texture() {
  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return texture;
}

// The clock's gradient, from #222 at the bottom of the ring to #444 at its
// top (top, in GL units), at height y. The ring and hands share it, so they
// look like one piece.
static void face_color(float y, float top, float alpha = 1) {
  float t = std::clamp((y / top + 1) * 0.5f, 0.0f, 1.0f);
  float v = (0x22 + (0x44 - 0x22) * t) / 255.0f;
  glColor4f(v, v, v, alpha);
}

// Blends over what's there, keeping its alpha premultiplied.
static void blend_over() {
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);
}

bool ClockWindow::setup_gl() {
  mFrostProgram = build_program(FROST_FRAG_SOURCE);
  mBlurProgram = build_program(BLUR_FRAG_SOURCE);
  if (!mFrostProgram || !mBlurProgram) {
    return false;
  }

  mBackdropTexture = create_texture();
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mBackdrop.width, mBackdrop.height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, mBackdrop.pixels.data());
  mLayerTexture = create_texture();
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

bool ClockWindow::setup_font() {
  if (!dynload_setup::freetype()) {
    return false;
  }
  if (FT_Init_FreeType(&mFreetype) != 0) {
    fprintf(stderr, "tcc_clock: FT_Init_FreeType failed\n");
    mFreetype = nullptr;
    return false;
  }
  if (FT_New_Memory_Face(mFreetype, OpenSans_Semibold.data(),
                         OpenSans_Semibold.size(), 0, &mFont) != 0 ||
      FT_Set_Pixel_Sizes(mFont, 0, TEXT_SIZE * mScale) != 0) {
    fprintf(stderr, "tcc_clock: couldn't load the font\n");
    mFont = nullptr;
    return false;
  }
  return true;
}

void ClockWindow::teardown_gl() {
  if (mEGLContext != EGL_NO_CONTEXT) {
    GLuint textures[] = {mBackdropTexture, mLayerTexture, mDate.texture,
                         mTime.texture};
    glDeleteTextures(4, textures);
  }
  if (mFont)
    FT_Done_Face(mFont);
  if (mFreetype)
    FT_Done_FreeType(mFreetype);
}

void ClockWindow::render_text(Text *text) {
  if (!mFont) {
    return;
  }

  int advance = 0;
  for (const char *c = text->string; *c; c++) {
    if (FT_Load_Char(mFont, (unsigned char)*c, FT_LOAD_DEFAULT) == 0) {
      advance += mFont->glyph->advance.x >> 6;
    }
  }
  // (the descender's negative)
  int ascender = mFont->size->metrics.ascender >> 6;
  int descender = mFont->size->metrics.descender >> 6;
  // (rows a multiple of 4 bytes long, as GL expects them by default)
  int w = (advance + TEXT_PADDING * 2 + 3) & ~3;
  int h = ascender - descender + TEXT_PADDING * 2;
  std::vector<unsigned char> coverage(w * h, 0);

  int pen = TEXT_PADDING, baseline = TEXT_PADDING + ascender;
  for (const char *c = text->string; *c; c++) {
    if (FT_Load_Char(mFont, (unsigned char)*c, FT_LOAD_RENDER) != 0) {
      continue;
    }
    FT_GlyphSlot glyph = mFont->glyph;
    const FT_Bitmap &bitmap = glyph->bitmap;
    int left = pen + glyph->bitmap_left, top = baseline - glyph->bitmap_top;
    for (int row = 0; row < (int)bitmap.rows; row++) {
      int y = top + row;
      for (int col = 0; col < (int)bitmap.width; col++) {
        int x = left + col;
        if (x >= 0 && x < w && y >= 0 && y < h) {
          unsigned char &p = coverage[y * w + x];
          p = std::min(255, p + bitmap.buffer[row * bitmap.pitch + col]);
        }
      }
    }
    pen += glyph->advance.x >> 6;
  }

  if (!text->texture) {
    text->texture = create_texture();
  } else {
    glBindTexture(GL_TEXTURE_2D, text->texture);
  }
  // (top row first)
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
               coverage.data());
  glBindTexture(GL_TEXTURE_2D, 0);
  text->width = w;
  text->height = h;
  text->advance = advance;
}

void ClockWindow::draw(double strength) {
  glViewport(0, 0, WINDOW_WIDTH * mScale, WINDOW_HEIGHT * mScale);
  if (shown(strength) < 1) {
    // The foreground goes on its own first, to be blurred and faded in over
    // the background.
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_foreground();
    blur_foreground(strength);
  } else {
    draw_background(strength);
    draw_foreground();
  }
  swap();
}

void ClockWindow::draw_background(double strength) {
  const float bw = WINDOW_WIDTH + 2 * BACKDROP_MARGIN;
  const float bh = WINDOW_HEIGHT + 2 * BACKDROP_MARGIN;
  // (the backdrop's top row is first, so v goes down)
  float u0 = BACKDROP_MARGIN / bw, u1 = (BACKDROP_MARGIN + WINDOW_WIDTH) / bw;
  float v0 = BACKDROP_MARGIN / bh, v1 = (BACKDROP_MARGIN + WINDOW_HEIGHT) / bh;

  glViewport(0, 0, WINDOW_WIDTH * mScale, WINDOW_HEIGHT * mScale);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glUseProgram(mFrostProgram);
  glBindTexture(GL_TEXTURE_2D, mBackdropTexture);
  glUniform1i(glGetUniformLocation(mFrostProgram, "backdrop"), 0);
  glUniform2f(glGetUniformLocation(mFrostProgram, "spread"), FROST_RADIUS / bw,
              FROST_RADIUS / bh);
  glUniform3f(glGetUniformLocation(mFrostProgram, "tint"), TINT, TINT, TINT);
  glUniform1f(glGetUniformLocation(mFrostProgram, "strength"), strength);
  glUniform2f(glGetUniformLocation(mFrostProgram, "backdrop_size"), bw, bh);
  glUniform4f(glGetUniformLocation(mFrostProgram, "window"), BACKDROP_MARGIN,
              BACKDROP_MARGIN, WINDOW_WIDTH, WINDOW_HEIGHT);
  glUniform1f(glGetUniformLocation(mFrostProgram, "radius"), CORNER_RADIUS);

  glBegin(GL_QUADS);
  glTexCoord2f(u0, v1);
  glVertex2f(-1, -1);
  glTexCoord2f(u1, v1);
  glVertex2f(1, -1);
  glTexCoord2f(u1, v0);
  glVertex2f(1, 1);
  glTexCoord2f(u0, v0);
  glVertex2f(-1, 1);
  glEnd();

  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

void ClockWindow::draw_foreground() {
  const int w = WINDOW_WIDTH * mScale, h = WINDOW_HEIGHT * mScale;
  const int header = HEADER_HEIGHT * mScale;

  // The date and time, each in the middle of its half of the header, in
  // pixels from the top left.
  glViewport(0, 0, w, h);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, w, h, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  draw_text(mDate, 0, 0, w / 2, header);
  draw_text(mTime, w / 2, 0, w - w / 2, header);

  // The face, in the square below.
  glViewport(0, 0, w, h - header);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-1, 1, -1, 1, 0, 100);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  draw_ring(RING_RADIUS, RING_WIDTH * mScale, w);
  draw_hand(deg2rad((mTm.tm_hour % 12) * 30 + (mTm.tm_min / 2.) - 90), 0.3,
            HOUR_HAND_WIDTH * mScale, w);
  draw_hand(deg2rad(mTm.tm_min * 6 - 90), 0.5, MINUTE_HAND_WIDTH * mScale, w);

  glViewport(0, 0, w, h);
}

// Draws text in the middle of the area (x, y, w, h), in pixels from the top
// left.
void ClockWindow::draw_text(const Text &text, int x, int y, int w, int h) {
  if (!text.texture) {
    return;
  }
  float left = x + (w - text.advance) / 2. - TEXT_PADDING;
  float top = y + (h - text.height) / 2.;

  blend_over();
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, text.texture);
  glColor4f(TEXT_COLOR, TEXT_COLOR, TEXT_COLOR, 1);

  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex2f(left, top);
  glTexCoord2f(1, 0);
  glVertex2f(left + text.width, top);
  glTexCoord2f(1, 1);
  glVertex2f(left + text.width, top + text.height);
  glTexCoord2f(0, 1);
  glVertex2f(left, top + text.height);
  glEnd();

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
}

// Draws a circle around the middle, radius across (in GL units) and width
// pixels wide on a face size pixels across.
void ClockWindow::draw_ring(double radius, float width, int size) {
  const int segments = 128;
  float half = width / size;
  float pixel = 2.0f / size;
  float outer = radius + half, inner = radius - half;
  float top = RING_RADIUS + RING_WIDTH * mScale / size;

  // The band between the radii r0 and r1, going from alpha a0 to a1.
  auto band = [&](float r0, float a0, float r1, float a1) {
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= segments; i++) {
      double rad = 2 * M_PI * i / segments;
      float x = cos(rad), y = sin(rad);
      face_color(y * r0, top, a0);
      glVertex2f(x * r0, y * r0);
      face_color(y * r1, top, a1);
      glVertex2f(x * r1, y * r1);
    }
    glEnd();
  };

  // Solid in the middle, fading out over a pixel at each edge so they're
  // smooth.
  blend_over();
  band(outer + pixel / 2, 0, outer - pixel / 2, 1);
  band(outer - pixel / 2, 1, inner + pixel / 2, 1);
  band(inner + pixel / 2, 1, inner - pixel / 2, 0);
  glDisable(GL_BLEND);
}

// Draws a hand from the middle out, at angle rad, length long (in GL units)
// and width pixels wide on a face size pixels across.
void ClockWindow::draw_hand(double rad, double length, float width, int size) {
  float dx = cos(rad), dy = sin(rad);
  float half = width / size;
  // (half a pixel)
  float feather = 1.0f / size;
  float top = RING_RADIUS + RING_WIDTH * mScale / size;

  // The hand's corners, along it from 0 to length and across it from -half
  // to half, grown by grow on every side.
  auto corners = [&](float grow, float out[4][2]) {
    const float along[4] = {-grow, (float)length + grow, (float)length + grow,
                            -grow};
    const float across[4] = {half + grow, half + grow, -half - grow,
                             -half - grow};
    for (int i = 0; i < 4; i++) {
      out[i][0] = dx * along[i] - dy * across[i];
      out[i][1] = dy * along[i] + dx * across[i];
    }
  };
  float inner[4][2], outer[4][2];
  corners(-feather, inner);
  corners(feather, outer);

  auto vertex = [&](float *c, float alpha) {
    face_color(c[1], top, alpha);
    glVertex2f(c[0], c[1]);
  };

  // Solid in the middle, fading out over a pixel at the edges so they're
  // smooth.
  blend_over();
  glBegin(GL_QUADS);
  for (auto &c : inner) {
    vertex(c, 1);
  }
  glEnd();
  glBegin(GL_QUAD_STRIP);
  for (int i = 0; i <= 4; i++) {
    vertex(inner[i % 4], 1);
    vertex(outer[i % 4], 0);
  }
  glEnd();
  glDisable(GL_BLEND);
}

void ClockWindow::draw_layer(float dx, float dy, double sigma, double opacity) {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glUseProgram(mBlurProgram);
  glBindTexture(GL_TEXTURE_2D, mLayerTexture);
  glUniform1i(glGetUniformLocation(mBlurProgram, "layer"), 0);
  glUniform2f(glGetUniformLocation(mBlurProgram, "dir"), dx, dy);
  glUniform1f(glGetUniformLocation(mBlurProgram, "sigma"), sigma);
  glUniform1f(glGetUniformLocation(mBlurProgram, "opacity"), opacity);

  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex2f(-1, -1);
  glTexCoord2f(1, 0);
  glVertex2f(1, -1);
  glTexCoord2f(1, 1);
  glVertex2f(1, 1);
  glTexCoord2f(0, 1);
  glVertex2f(-1, 1);
  glEnd();

  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

void ClockWindow::blur_foreground(double strength) {
  const int w = WINDOW_WIDTH * mScale, h = WINDOW_HEIGHT * mScale;
  double sigma = blur_sigma(shown(strength));

  // Across, from a copy of the foreground back into the frame...
  glBindTexture(GL_TEXTURE_2D, mLayerTexture);
  glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, w, h, 0);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  draw_layer(1.0f / w, 0, sigma, 1);

  // ...then down, from a copy of that over the background.
  glBindTexture(GL_TEXTURE_2D, mLayerTexture);
  glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, w, h, 0);
  draw_background(strength);
  glEnable(GL_BLEND);
  // (the layer's alpha is premultiplied)
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  draw_layer(0, 1.0f / h, sigma, shown(strength));
  glDisable(GL_BLEND);
}
