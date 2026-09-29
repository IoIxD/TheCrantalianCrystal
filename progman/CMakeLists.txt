project(tcc_progman)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

file(GLOB_RECURSE sources *.cpp *.c)

add_library(${PROJECT_NAME} SHARED ${sources})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/submodules/milsko/include)
target_link_libraries(${PROJECT_NAME} PRIVATE Mw tcc_iconlib)

# gtk4 (and glib/gio) are dlopen'd at runtime, only their headers are needed
pkg_check_modules(progman_deps REQUIRED glib-2.0 gio-2.0 gio-unix-2.0 gtk4)
target_include_directories(${PROJECT_NAME} PRIVATE ${progman_deps_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${progman_deps_CFLAGS_OTHER})
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
