#include "glyph.hpp"
#include "../client/font.hpp"
#include "texture.hpp"
#include "utf8.hpp"
#include <assert.h>
#include <cstdio>
#include <cstdlib>

FT_Library GlyphManager::FTLibrary;
FT_Face GlyphManager::FTFaceNormal;
FT_Face GlyphManager::FTFaceBold;

void GlyphManager::Init() {
  assert(FT_Init_FreeType(&FTLibrary) == 0);
  assert(FT_New_Memory_Face(FTLibrary, OpenSans_Regular.data(),
                            OpenSans_Regular.size(), 0, &FTFaceNormal) == 0);
  assert(FT_New_Memory_Face(FTLibrary, OpenSans_Semibold.data(),
                            OpenSans_Semibold.size(), 0, &FTFaceBold) == 0);
  assert(FT_Set_Pixel_Sizes(FTFaceNormal, 0, 13) == 0);
  assert(FT_Set_Pixel_Sizes(FTFaceBold, 0, 13) == 0);
}

void GlyphManager::Deinit() {
  assert(FT_Done_Face(FTFaceNormal) == 0);
  assert(FT_Done_Face(FTFaceBold) == 0);
  assert(FT_Done_FreeType(FTLibrary) == 0);
}

GlyphManager::Glyph::~Glyph() { TextureManager::FreeGLTextureID(texture); }

// Loads and caches a glyph's texture the first time it's needed
std::shared_ptr<GlyphManager::Glyph>
GlyphManager::get_glyph(uint32_t c, bool bold, bool black) {
  FT_Face f = bold ? FTFaceBold : FTFaceNormal;
  auto &cache = bold ? (black ? mGlyphCacheBoldBlack : mGlyphCacheBoldWhite)
                     : (black ? mGlyphCacheBlack : mGlyphCacheWhite);
  FT_GlyphSlot slot = f->glyph;
  FT_Bitmap *bmp = &slot->bitmap;

  if (cache.contains(c)) {
    return cache.at(c);
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
      rgba[idx + 0] = black ? 0 : 255;
      rgba[idx + 1] = black ? 0 : 255;
      rgba[idx + 2] = black ? 0 : 255;
      rgba[idx + 3] = alpha;
    }
  }

  g->texture = TextureManager::NewGLTextureID(w, h, rgba);

  free(rgba);
  g->loaded = 1;

  cache.insert_or_assign(c, g);
  return cache.at(c);
}

void GlyphManager::draw_text(std::string text, int32_t x, int32_t y,
                             int32_t width, int32_t height, bool bold,
                             bool black) {
  glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);

  float penX = x;

  for (auto codepoint : get_codepoints(text)) {
    auto g = get_glyph(codepoint, bold, black);

    if (g->width > 0 && g->height > 0) {
      float px0 = penX + g->bearingX;
      // top of glyph
      float py0 = (float)y - g->bearingY;
      float x0 = (px0 / width) * 2.0f - 1.0f;
      float y0 = 1.0f - (py0 / height) * 2.0f;
      float x1 = ((float)(px0 + g->width) / width) * 2.0f - 1.0f;
      // bottom of glyph
      float y1 = 1.0f - ((float)(py0 + g->height) / height) * 2.0f;

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
