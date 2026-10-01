project(tcc_dynload)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

find_package(PkgConfig REQUIRED)

add_library(${PROJECT_NAME} STATIC ${sources})
target_link_libraries(${PROJECT_NAME} PUBLIC ${CMAKE_DL_LIBS})
set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-changes-meaning")

# Dependencies that need to be avaliable at runtime

pkg_check_modules(deps REQUIRED
  glib-2.0
  gio-2.0
  gio-unix-2.0
  gtk4
  wayland-client
  gl
  egl
  wayland-egl
  freetype2
  libvarlink
  sqlite3
  xkbcommon
  gbm
)

# Dependencies that do not need to be avaliable at runtime.

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

include_directories(${deps_INCLUDE_DIRS})
include_directories(${deps_CFLAGS_OTHER})
