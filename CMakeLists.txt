cmake_minimum_required(VERSION 3.22...3.25)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME})
################################################################################
# include core files
include(core/core.cmake)
################################################################################

# submodule deps check
if(NOT
   (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/deps/websocketpp/CMakeLists.txt
    AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/deps/asio/asio/include/asio.hpp))
  message(FATAL_ERROR "submodule deps not available.")
endif()

# tell websocketpp not to use system boost
add_definitions(-DASIO_STANDALONE)
add_definitions(-D_WEBSOCKETPP_CPP11_STL_)

# Qt
find_qt(COMPONENTS Widgets Core Network)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Widgets Qt::Core Qt::Network)
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
)

# source files
target_sources(
  ${PROJECT_NAME}
  PRIVATE

  ### test files
  ${CMAKE_CURRENT_SOURCE_DIR}/test/main.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/test/TestMainWindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/test/TestMainWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test/TestMainWindow.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/test/SettingsWindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/test/SettingsWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test/SettingsWindow.ui

  ### websocket files
  ${CMAKE_CURRENT_SOURCE_DIR}/websocket/websocket.h
  ${CMAKE_CURRENT_SOURCE_DIR}/websocket/websocket.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/websocket/websocket-session.h
)

# include directories
target_include_directories(
  ${PROJECT_NAME}
  PRIVATE

  ${CMAKE_CURRENT_SOURCE_DIR}/deps/websocketpp
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/asio/asio/include
  ${CMAKE_CURRENT_SOURCE_DIR}/websocket
)

# link libraries
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE

  # extra link libraries
  # eg: ${CMAKE_CURRENT_SOURCE_DIR}/deps/extra_lib/libextra_lib.a
)
