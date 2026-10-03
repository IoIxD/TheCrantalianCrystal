project(tcc_clock)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

add_executable(${PROJECT_NAME} ${sources}
  ${CMAKE_SOURCE_DIR}/shell/protocol/wayland-client-protocol.cpp
  ${CMAKE_SOURCE_DIR}/shell/protocol/wlr-layer-shell-unstable-v1-protocol.cpp
  # (which the layer shell refers to)
  ${CMAKE_SOURCE_DIR}/shell/protocol/xdg-shell-protocol.cpp
)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/frost.frag" FROST_FRAG_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/blur.frag" BLUR_FRAG_SOURCE)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/frost_shader.h.in
  "${CMAKE_CURRENT_SOURCE_DIR}/frost_shader.h"
  @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS frost.frag)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS blur.frag)

# (just the headers; freetype itself is loaded at runtime, through dynload)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED freetype2)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/protocol
  ${CMAKE_SOURCE_DIR}/shell/protocol
  ${FREETYPE_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME} PUBLIC tcc_dynload tcc_fonts)
