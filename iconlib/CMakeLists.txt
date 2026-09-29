
project(tcc_iconlib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

find_package(PkgConfig REQUIRED)

add_library(${PROJECT_NAME} SHARED ${sources})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# gtk4 (and glib/gio) are dlopen'd at runtime, only their headers are needed
pkg_check_modules(iconlib_deps REQUIRED glib-2.0 gio-2.0 gio-unix-2.0 gtk4)
target_include_directories(${PROJECT_NAME} PRIVATE ${iconlib_deps_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${iconlib_deps_CFLAGS_OTHER})
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
