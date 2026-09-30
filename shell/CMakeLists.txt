
project(tcc_shell)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

find_package(PkgConfig REQUIRED)

add_executable(${PROJECT_NAME} ${sources})

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd.frag" SSD_FRAG_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd.vert" SSD_VERT_SOURCE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/client/ssd_shader.h.in "${CMAKE_CURRENT_SOURCE_DIR}/client/ssd_shader.h" @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS client/ssd.frag)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS client/ssd.vert)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/iconlib)
target_link_libraries(${PROJECT_NAME} PUBLIC
  tcc_iconlib
  # tcc_progman
  tcc_dynload
)
