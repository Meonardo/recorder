cmake_minimum_required(VERSION 3.22...3.25)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET OBS::frontend-api)
  add_subdirectory("${CMAKE_SOURCE_DIR}/UI/obs-frontend-api" "${CMAKE_BINARY_DIR}/UI/obs-frontend-api")
endif()

add_executable(${PROJECT_NAME})

# Qt
find_qt(COMPONENTS Widgets Core)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Widgets Qt::Core)
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON)

target_sources(
  ${PROJECT_NAME}
  PRIVATE

  ### source files
  ${CMAKE_CURRENT_SOURCE_DIR}/test/TestMainWindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/test/TestMainWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test/TestMainWindow.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/test/main.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/test/SettingsWindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/test/SettingsWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test/SettingsWindow.ui
)

# add corebridge source lib
add_subdirectory(corebridge)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE

  OBS::libobs
  OBS::corebridge
  OBS::frontend-api
)

set_property(
  TARGET ${PROJECT_NAME}
  APPEND
  PROPERTY AUTORCC_OPTIONS --format-version 1)

target_link_options(${PROJECT_NAME} PRIVATE /IGNORE:4098 /IGNORE:4099)

set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES WIN32_EXECUTABLE TRUE
             VS_DEBUGGER_COMMAND "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
             VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit")

get_property(obs_module_list GLOBAL PROPERTY OBS_MODULES_ENABLED)
list(JOIN obs_module_list "|" SAFE_MODULES)
target_compile_definitions(${PROJECT_NAME} PRIVATE "SAFE_MODULES=\"${SAFE_MODULES}\"")

if(CMAKE_COMPILE_WARNING_AS_ERROR)
  add_link_options(/WX-)
endif()

# cmake-format: off
set_target_properties_obs(${PROJECT_NAME} PROPERTIES FOLDER app OUTPUT_NAME "$<IF:$<PLATFORM_ID:Windows>,${PROJECT_NAME}64,${PROJECT_NAME}>")
# cmake-format: on
