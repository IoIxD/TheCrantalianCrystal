project(tcc_portal)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

# The portal is nothing but a D-Bus service.
pkg_check_modules(DBUS dbus-1)
if(NOT DBUS_FOUND)
  message(STATUS "dbus-1 not found, not building tcc_portal")
  return()
endif()

file(GLOB_RECURSE sources *.cpp)

add_executable(${PROJECT_NAME} ${sources})
target_link_libraries(${PROJECT_NAME} PRIVATE tcc_dynload)
target_include_directories(${PROJECT_NAME} PRIVATE
  ${DBUS_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/bridge
  ${CMAKE_CURRENT_SOURCE_DIR}/service
  ${CMAKE_CURRENT_BINARY_DIR})

# Embeds the varlink interfaces (VARLINK_<Name>) and the D-Bus introspection
# files the bridge works from (PORTAL_DBUS_XML). Written via configure_file()
# so they're only touched when they change.
file(GLOB varlink_files ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/*.varlink)
file(GLOB dbus_files ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/*.xml)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${varlink_files} ${dbus_files})

set(varlink_header "#pragma once\n\n")
foreach(file ${varlink_files})
  file(READ ${file} contents)
  string(REGEX REPLACE ".*\\.([A-Za-z]+)\\.varlink$" "\\1" name ${file})
  string(APPEND varlink_header
    "static const char *VARLINK_${name} = R\"__TCC__(${contents})__TCC__\";\n\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/portal_varlink.h.tmp "${varlink_header}")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/portal_varlink.h.tmp
  ${CMAKE_CURRENT_BINARY_DIR}/portal_varlink.h COPYONLY)

set(dbus_header "#pragma once\n\nstatic const char *PORTAL_DBUS_XML[] = {\n")
foreach(file ${dbus_files})
  file(READ ${file} contents)
  string(APPEND dbus_header "R\"__TCC__(${contents})__TCC__\",\n")
endforeach()
string(APPEND dbus_header "};\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/portal_dbus.h.tmp "${dbus_header}")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/portal_dbus.h.tmp
  ${CMAKE_CURRENT_BINARY_DIR}/portal_dbus.h COPYONLY)

set(TCC_PORTAL_EXEC
  ${CMAKE_INSTALL_FULL_LIBEXECDIR}/TheCrantalianCrystal/tcc_portal)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/org.freedesktop.impl.portal.desktop.tcc.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.tcc.service
  @ONLY)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/tcc.portal
  DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.tcc.service
  DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
