cmake_minimum_required(VERSION 4.3)

project(TheCrantalianCrystal)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

# === Code ===

add_subdirectory(submodules/milsko)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/submodules/milsko/include)

add_subdirectory(dynload)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dynload)

add_subdirectory(iconlib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/iconlib)

add_subdirectory(varlink)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/varlink)

add_subdirectory(registry)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/registry/lib)

add_subdirectory(screensaver)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/screensaver/loader)

add_subdirectory(shell)
add_subdirectory(progman)
add_subdirectory(portal)
add_subdirectory(systray)
add_subdirectory(clock)

# === Install ===

set(TCC_INSTALL_DIR ${CMAKE_INSTALL_LIBEXECDIR}/TheCrantalianCrystal)
set(TCC_PAM_DIR /etc/pam.d CACHE PATH "Where to install the PAM service file")

set(TCC_INSTALL_TARGETS
  tcc_shell
  tcc_progman
  tcc_systray
  tcc_scr_loader
  tcc_scr_tunnel
  tcc_iconlib
  tcc_varlinklib
  tcc_registry
  tcc_registrylib
)
if(TARGET tcc_portal)
  list(APPEND TCC_INSTALL_TARGETS tcc_portal)
endif()

file(RELATIVE_PATH TCC_LIBDIR_FROM_INSTALL_DIR
  ${CMAKE_INSTALL_FULL_LIBEXECDIR}/TheCrantalianCrystal
  ${CMAKE_INSTALL_FULL_LIBDIR})
set_target_properties(${TCC_INSTALL_TARGETS} PROPERTIES
  INSTALL_RPATH "$ORIGIN;$ORIGIN/${TCC_LIBDIR_FROM_INSTALL_DIR}")

install(TARGETS ${TCC_INSTALL_TARGETS}
  RUNTIME DESTINATION ${TCC_INSTALL_DIR}
  LIBRARY DESTINATION ${TCC_INSTALL_DIR})

file(RELATIVE_PATH TCC_INSTALL_DIR_FROM_BINDIR
  ${CMAKE_INSTALL_FULL_BINDIR}
  ${CMAKE_INSTALL_FULL_LIBEXECDIR}/TheCrantalianCrystal)
install(CODE "
  file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}\")
  file(CREATE_LINK ${TCC_INSTALL_DIR_FROM_BINDIR}/tcc_shell
    \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}/tcc_shell\" SYMBOLIC)
")

install(FILES shell/lock/tcc-lock.pam
  DESTINATION ${TCC_PAM_DIR}
  RENAME tcc-lock)
