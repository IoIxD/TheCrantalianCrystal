#include "utf8.hpp"
#include <cstdint>
#include <vector>

std::vector<uint32_t> get_codepoints(std::string str) {
  std::string str_ptr = str;
  const unsigned char *p = (const unsigned char *)str_ptr.c_str();
  std::vector<uint32_t> codepoints;

  while (*p) {
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
    codepoints.push_back(codepoint);
  }
  return codepoints;
}
