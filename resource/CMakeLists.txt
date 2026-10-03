project(tcc_resources)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Embeds a file as a byte array named after it, defined in a generated source
# appended to out_sources.
function(tcc_embed_resource file name out_sources)
  set(path "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
  set(source "${CMAKE_CURRENT_BINARY_DIR}/embedded/${name}.cpp")

  file(READ "${path}" hex HEX)
  string(LENGTH "${hex}" length)
  math(EXPR size "${length} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")

  file(CONFIGURE OUTPUT "${source}" CONTENT
"#include \"fonts.hpp\"

static const unsigned char data[${size}] = {${bytes}};
const std::span<const unsigned char> ${name}(data, sizeof(data));
" @ONLY)

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
  set(${out_sources} ${${out_sources}} "${source}" PARENT_SCOPE)
endfunction()

set(font_sources)
tcc_embed_resource(fonts/OpenSans-Regular.ttf OpenSans_Regular font_sources)
tcc_embed_resource(fonts/OpenSans-Semibold.ttf OpenSans_Semibold font_sources)

add_library(tcc_fonts STATIC ${font_sources})
target_include_directories(tcc_fonts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/fonts)
set_property(TARGET tcc_fonts PROPERTY POSITION_INDEPENDENT_CODE ON)
