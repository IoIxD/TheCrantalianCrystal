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

  void *handle = nullptr;
};

extern FreetypeLib *FT_LIB;
#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define FT_Init_FreeType FT_LIB->FT_Init_FreeType
#define FT_Done_FreeType FT_LIB->FT_Done_FreeType
#define FT_New_Memory_Face FT_LIB->FT_New_Memory_Face
#define FT_Done_Face FT_LIB->FT_Done_Face
#define FT_Set_Pixel_Sizes FT_LIB->FT_Set_Pixel_Sizes
#define FT_Load_Char FT_LIB->FT_Load_Char
#endif
