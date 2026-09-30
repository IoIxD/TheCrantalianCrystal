project(tcc_registry_daemon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

add_executable(${PROJECT_NAME} ${sources})

file(READ
  "${CMAKE_CURRENT_SOURCE_DIR}/interface.varlink"
  VARLINK_INTERFACE_SOURCE
)

target_link_libraries(${PROJECT_NAME} PUBLIC tcc_dynload Mw)
configure_file(interface.varlink.h.in "${CMAKE_CURRENT_SOURCE_DIR}/interface.varlink.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/interface.varlink)
