project(tcc_scr_loader)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

add_executable(${PROJECT_NAME} ${sources})

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/blur.frag" BLUR_FRAG_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/composite.frag" COMPOSITE_FRAG_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/quad.vert" QUAD_VERT_SOURCE)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders.h.in
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders.h"
  @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS blur.frag)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS composite.frag)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS quad.vert)

target_link_libraries(${PROJECT_NAME} PUBLIC tcc_dynload)
