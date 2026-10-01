project(tcc_registry)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The client library, for everything that reads or writes settings.
file(GLOB_RECURSE lib_sources lib/*.cpp)

add_library(tcc_registrylib SHARED ${lib_sources})
target_link_libraries(tcc_registrylib PUBLIC tcc_dynload)
target_include_directories(tcc_registrylib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# The daemon.
file(GLOB_RECURSE daemon_sources daemon/*.cpp)

add_executable(${PROJECT_NAME} ${daemon_sources})
target_link_libraries(${PROJECT_NAME} PRIVATE tcc_dynload)
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib
  ${CMAKE_CURRENT_BINARY_DIR})

# Embeds the interface (TCC_REGISTRY_INTERFACE_SOURCE). Written via
# configure_file() so it's only touched when it changes.
set(interface_file
  ${CMAKE_CURRENT_SOURCE_DIR}/net.ioi-xd.tcc.registry.varlink)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${interface_file})
file(READ ${interface_file} contents)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/registry_interface.h.tmp
  "#pragma once\n\nstatic const char *TCC_REGISTRY_INTERFACE_SOURCE = R\"__TCC__(${contents})__TCC__\";\n")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/registry_interface.h.tmp
  ${CMAKE_CURRENT_BINARY_DIR}/registry_interface.h COPYONLY)
