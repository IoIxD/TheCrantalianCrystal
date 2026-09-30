project(tcc_scr_loader)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

# The shell's vendored protocol code goes through the dlopen'd wayland-client,
# so it's compiled straight in rather than linking libwayland-client.
# wlr-layer-shell refers to xdg_popup, so xdg-shell's code is needed too.
set(PROTOCOL_DIR ${CMAKE_SOURCE_DIR}/shell/protocol)
list(APPEND sources
  ${PROTOCOL_DIR}/wayland-client-protocol.cpp
  ${PROTOCOL_DIR}/xdg-shell-protocol.cpp
  ${PROTOCOL_DIR}/wlr-layer-shell-unstable-v1-protocol.cpp
)

add_executable(${PROJECT_NAME} ${sources})

target_include_directories(${PROJECT_NAME} PRIVATE ${PROTOCOL_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC tcc_dynload)
