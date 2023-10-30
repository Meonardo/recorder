cmake_minimum_required(VERSION 3.22...3.25)

project(recorder VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME})

target_sources(
  ${PROJECT_NAME}
  PRIVATE

  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

set(OBS_VERSION_OVERRIDE "29.0.0")
set(OBS_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/obs-studio")
message(STATUS "OBS_ROOT_DIR: ${OBS_ROOT_DIR}")

add_subdirectory(${OBS_ROOT_DIR})