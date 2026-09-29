
project(tcc_iconlib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

find_package(PkgConfig REQUIRED)

add_library(${PROJECT_NAME} SHARED ${sources})
include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

pkg_check_modules(deps REQUIRED
  glib-2.0
  gio-2.0
  gio-unix-2.0
  gtk4
)
target_include_directories(${PROJECT_NAME} PUBLIC ${deps_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PUBLIC ${deps_CFLAGS_OTHER})
