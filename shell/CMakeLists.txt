
project(tcc_shell)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

find_package(PkgConfig REQUIRED)

add_executable(${PROJECT_NAME} ${sources})

# wayland-client, GL, EGL, wayland-egl and freetype are dlopen'd at runtime,
# only their headers are needed
pkg_check_modules(shell_deps REQUIRED wayland-client gl egl wayland-egl
                  freetype2)
target_include_directories(${PROJECT_NAME} PRIVATE ${shell_deps_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${shell_deps_CFLAGS_OTHER})
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd.frag" SSD_FRAG_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd.vert" SSD_VERT_SOURCE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/client/ssd_shader.h.in "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd_shader.h" @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS client/ssd.frag)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS client/ssd.vert)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/iconlib)
target_link_libraries(${PROJECT_NAME} PRIVATE tcc_iconlib)
target_link_libraries(${PROJECT_NAME} PRIVATE tcc_progman)
