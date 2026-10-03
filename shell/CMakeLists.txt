
project(tcc_shell)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

find_package(PkgConfig REQUIRED)

add_executable(${PROJECT_NAME} ${sources})

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd.frag" SSD_FRAG_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd.vert" SSD_VERT_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lock/lock_ssd.frag" LOCK_SSD_FRAG_SOURCE)
file(READ
  "${CMAKE_CURRENT_SOURCE_DIR}/varlink/interface.varlink"
  VARLINK_INTERFACE_SOURCE
)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/client/ssd_shader.h.in
  "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd_shader.h"
  @ONLY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/lock/lock_ssd_shader.h.in
  "${CMAKE_CURRENT_SOURCE_DIR}/lock/lock_ssd_shader.h"
  @ONLY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/varlink/interface.varlink.h.in
  "${CMAKE_CURRENT_SOURCE_DIR}/varlink/interface.varlink.h"
@ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS client/ssd.frag)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS client/ssd.vert)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS lock/lock_ssd.frag)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/varlink/interface.varlink)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/iconlib)
# For scr_ipc.hpp, shared with the screensaver loader.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/screensaver)
target_link_libraries(${PROJECT_NAME} PUBLIC
  tcc_iconlib
  tcc_registrylib
  tcc_dynload
  tcc_fonts
)

# For logind, see lock/logind.cpp.
pkg_check_modules(DBUS dbus-1)
if(DBUS_FOUND)
  target_include_directories(${PROJECT_NAME} PRIVATE ${DBUS_INCLUDE_DIRS})
endif()
