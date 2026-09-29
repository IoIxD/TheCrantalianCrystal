project(tcc_systray)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

file(GLOB_RECURSE sources *.cpp *.c)

add_executable(${PROJECT_NAME} ${sources})
target_link_libraries(${PROJECT_NAME} PRIVATE Mw tcc_iconlib tcc_dynload)

pkg_check_modules(DBUS dbus-1)
pkg_check_modules(PULSE libpulse)

if(DBUS_FOUND)
  include_directories(${DBUS_INCLUDE_DIRS})
  target_compile_definitions(${PROJECT_NAME} PUBLIC TCC_HAS_DBUS)
endif()
if(PULSE_FOUND)
  include_directories(${PULSE_INCLUDE_DIRS})
  target_compile_definitions(${PROJECT_NAME} PUBLIC TCC_HAS_PULSE)
endif()

# river's xkb config protocol, for the keyboard layout item. The protocol code
# is generated from the shell's copies of river's protocol files.
#
# wayland-client is dlopen'd at runtime, so the core protocol's code is
# generated here too instead of coming from the library.
pkg_check_modules(WAYLAND wayland-client)
pkg_check_modules(WAYLAND_SCANNER_PC wayland-scanner)
find_program(WAYLAND_SCANNER wayland-scanner)
if(WAYLAND_FOUND AND WAYLAND_SCANNER AND WAYLAND_SCANNER_PC_FOUND)
  set(RIVER_PROTOCOL_DIR ${CMAKE_SOURCE_DIR}/shell/protocol)
  set(RIVER_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocol)
  file(MAKE_DIRECTORY ${RIVER_GENERATED_DIR})
  # river-xkb-config-v1 refers to river_input_device_v1 from the input management
  # protocol, so that one's code is needed too.
  foreach(proto river-xkb-config-v1 river-input-management-v1)
    add_custom_command(
      OUTPUT ${RIVER_GENERATED_DIR}/${proto}-protocol.h
             ${RIVER_GENERATED_DIR}/${proto}-protocol.c
      COMMAND ${WAYLAND_SCANNER} client-header ${RIVER_PROTOCOL_DIR}/${proto}.xml
              ${RIVER_GENERATED_DIR}/${proto}-protocol.h
      COMMAND ${WAYLAND_SCANNER} private-code ${RIVER_PROTOCOL_DIR}/${proto}.xml
              ${RIVER_GENERATED_DIR}/${proto}-protocol.c
      DEPENDS ${RIVER_PROTOCOL_DIR}/${proto}.xml)
    target_sources(${PROJECT_NAME} PRIVATE
                   ${RIVER_GENERATED_DIR}/${proto}-protocol.h
                   ${RIVER_GENERATED_DIR}/${proto}-protocol.c)
  endforeach()
  pkg_get_variable(WAYLAND_PROTOCOL_DIR wayland-scanner pkgdatadir)
  add_custom_command(
    OUTPUT ${RIVER_GENERATED_DIR}/wayland-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${WAYLAND_PROTOCOL_DIR}/wayland.xml
            ${RIVER_GENERATED_DIR}/wayland-protocol.c
    DEPENDS ${WAYLAND_PROTOCOL_DIR}/wayland.xml)
  target_sources(${PROJECT_NAME} PRIVATE
                 ${RIVER_GENERATED_DIR}/wayland-protocol.c)
  target_include_directories(${PROJECT_NAME} PRIVATE ${RIVER_GENERATED_DIR}
                             ${WAYLAND_INCLUDE_DIRS})
  target_compile_definitions(${PROJECT_NAME} PRIVATE TCC_SYSTRAY_RIVER)
endif()
