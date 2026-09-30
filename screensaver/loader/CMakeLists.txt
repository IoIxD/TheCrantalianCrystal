project(tcc_scr_loader)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources *.cpp *.c)

add_executable(${PROJECT_NAME} ${sources})

# For scr_ipc.hpp, shared with the shell.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/screensaver)
target_link_libraries(${PROJECT_NAME} PUBLIC tcc_dynload)
