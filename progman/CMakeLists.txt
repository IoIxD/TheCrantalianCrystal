project(tcc_progman)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

file(GLOB_RECURSE sources *.cpp *.c)

# add_library(${PROJECT_NAME} SHARED ${sources})
add_executable(${PROJECT_NAME} ${sources})
target_link_libraries(${PROJECT_NAME} PRIVATE
  Mw
  tcc_iconlib
  tcc_dynload
  tcc_registrylib
)
