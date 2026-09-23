#pragma once

#include <cstdint>
#include <string>
#include <vector>
/* extract utf8 codepoints from the string. */
std::vector<uint32_t> get_codepoints(std::string str);
