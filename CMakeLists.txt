cmake_minimum_required(VERSION 3.22...3.25)

project(recorder VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE
)

target_sources(
  ${PROJECT_NAME}
  PRIVATE

  main.cpp
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
)
