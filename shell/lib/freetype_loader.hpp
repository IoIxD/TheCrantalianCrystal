#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H

#define TCC_FREETYPE_FUNCS(X)                                                  \
  X(FT_Init_FreeType)                                                          \
  X(FT_Done_FreeType)                                                          \
  X(FT_New_Memory_Face)                                                        \
  X(FT_Done_Face)                                                              \
  X(FT_Set_Pixel_Sizes)                                                        \
  X(FT_Load_Char)

struct FreetypeLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_FREETYPE_FUNCS(X)
#undef X

  // Returns the process-wide instance, or nullptr if freetype could not be
  // loaded.
  static FreetypeLib *get();

private:
  void *mHandle = nullptr;
  bool load();
};
