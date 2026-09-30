project(tcc_scr_tunnel)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

add_library(${PROJECT_NAME} SHARED ${sources})

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/tunnel.vert" TUNNEL_VERT_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/tunnel.frag" TUNNEL_FRAG_SOURCE)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/tunnel_shader.h.in
  "${CMAKE_CURRENT_SOURCE_DIR}/tunnel_shader.h"
  @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tunnel.vert)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tunnel.frag)

target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
