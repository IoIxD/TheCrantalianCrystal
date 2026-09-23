#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <ft2build.h>
#include FT_FREETYPE_H

class GlyphManager {
  static FT_Library FTLibrary;
  static FT_Face FTFaceNormal;
  static FT_Face FTFaceBold;

public:
  class Glyph {
  public:
    GLuint texture = 0;
    int width = 0, height = 0;
    int bearingX = 0, bearingY = 0;
    long advance = 0;
    int loaded = 0;
    ~Glyph();
  };

  static void Init();
  static void Deinit();

  void draw_text(std::string text, int32_t x, int32_t y, int32_t width,
                 int32_t height, bool bold, bool black);

private:
  std::shared_ptr<Glyph> get_glyph(uint32_t c, bool bold, bool black);

  std::unordered_map<uint32_t, std::shared_ptr<Glyph>> mGlyphCacheWhite;
  std::unordered_map<uint32_t, std::shared_ptr<Glyph>> mGlyphCacheBoldWhite;
  std::unordered_map<uint32_t, std::shared_ptr<Glyph>> mGlyphCacheBlack;
  std::unordered_map<uint32_t, std::shared_ptr<Glyph>> mGlyphCacheBoldBlack;
};
