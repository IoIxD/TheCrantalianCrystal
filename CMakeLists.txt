cmake_minimum_required(VERSION 4.3)

project(TheCrantalianCrystal)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

add_subdirectory(submodules/milsko)

add_subdirectory(dynload)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dynload)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/iconlib)

add_subdirectory(iconlib)
add_subdirectory(shell)
add_subdirectory(progman)
add_subdirectory(systray)
