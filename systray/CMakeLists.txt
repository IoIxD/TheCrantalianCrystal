project(tcc_systray)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

file(GLOB_RECURSE sources *.cpp *.c)

add_executable(${PROJECT_NAME} ${sources})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/submodules/milsko/include)
target_link_libraries(${PROJECT_NAME} PRIVATE Mw tcc_iconlib)

pkg_check_modules(DBUS dbus-1)
if(DBUS_FOUND)
  target_include_directories(${PROJECT_NAME} PRIVATE ${DBUS_INCLUDE_DIRS})
  target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
  target_compile_definitions(${PROJECT_NAME} PRIVATE TCC_SYSTRAY_DBUS)
endif()
