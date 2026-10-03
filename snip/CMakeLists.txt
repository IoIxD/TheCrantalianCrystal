project(tcc_snip)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)
# The clock's capture code, for what's on screen before the snip shows up.
file(GLOB capture_protocols ${CMAKE_SOURCE_DIR}/clock/protocol/*.cpp)

add_executable(${PROJECT_NAME} ${sources}
  ${CMAKE_SOURCE_DIR}/clock/capture.cpp
  ${capture_protocols}
  ${CMAKE_SOURCE_DIR}/shell/protocol/wayland-client-protocol.cpp
  ${CMAKE_SOURCE_DIR}/shell/protocol/wlr-layer-shell-unstable-v1-protocol.cpp
  ${CMAKE_SOURCE_DIR}/shell/protocol/cursor-shape-v1-protocol.cpp
  # (which the two above refer to)
  ${CMAKE_SOURCE_DIR}/shell/protocol/xdg-shell-protocol.cpp
  ${CMAKE_SOURCE_DIR}/shell/protocol/tablet-v2-protocol.cpp
)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/frost.frag" FROST_FRAG_SOURCE)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/frost_shader.h.in
  "${CMAKE_CURRENT_SOURCE_DIR}/frost_shader.h"
  @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS frost.frag)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/protocol
  ${CMAKE_SOURCE_DIR}/clock
  ${CMAKE_SOURCE_DIR}/clock/protocol
  ${CMAKE_SOURCE_DIR}/shell/protocol
)
target_link_libraries(${PROJECT_NAME} PUBLIC tcc_dynload)
