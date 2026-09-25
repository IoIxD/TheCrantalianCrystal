project(tcc_progman)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

file(GLOB_RECURSE sources *.cpp *.c)

add_subdirectory(submodules/milsko)

add_executable(${PROJECT_NAME} ${sources})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/submodules/milsko/include)
target_link_libraries(${PROJECT_NAME} PRIVATE Mw)

pkg_check_modules(deps REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0 gtk4)
target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::deps)
