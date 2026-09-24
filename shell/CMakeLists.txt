
project(tcc_shell)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

find_package(Freetype REQUIRED)
find_package(PkgConfig REQUIRED)

add_executable(${PROJECT_NAME} ${sources})
target_link_libraries(${PROJECT_NAME} PRIVATE wayland-client wayland-egl EGL GL)
target_link_libraries(${PROJECT_NAME} PRIVATE ${FREETYPE_LIBRARIES})
target_include_directories(${PROJECT_NAME} PRIVATE ${FREETYPE_INCLUDE_DIRS})

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd.frag" SSD_FRAG_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd.vert" SSD_VERT_SOURCE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/client/ssd_shader.h.in "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd_shader.h" @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS client/ssd.frag)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS client/ssd.vert)

pkg_check_modules(deps REQUIRED IMPORTED_TARGET glib-2.0 gtk4)
target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::deps)
