cmake_minimum_required(VERSION 3.22...3.25)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET OBS::w32-pthreads)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/w32-pthreads" "${CMAKE_BINARY_DIR}/deps/w32-pthreads")
endif()

if(NOT TARGET OBS::frontend-api)
  add_subdirectory("${CMAKE_SOURCE_DIR}/UI/obs-frontend-api" "${CMAKE_BINARY_DIR}/UI/obs-frontend-api")
endif()

if(NOT TARGET OBS::json11)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/json11" "${CMAKE_BINARY_DIR}/deps/json11")
endif()

add_executable(recorder)

find_package(Detours REQUIRED)

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

  # core app
  ${CMAKE_CURRENT_SOURCE_DIR}/core/app.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/app.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/app-profile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/utils.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/ui.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/ui.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/output.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/output.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/preview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/preview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/scene-source.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/scene-source.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/defines.h

  ${CMAKE_CURRENT_SOURCE_DIR}/test/TestMainWindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/test/TestMainWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test/TestMainWindow.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/test/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test/test.h

  ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/window-main.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SourceDuplicatorWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SourceDuplicatorWindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SourceDuplicatorWindow.ui

  ${CMAKE_CURRENT_SOURCE_DIR}/src/window-basic-preview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/window-basic-preview.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qt-display.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qt-display.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qt-wrappers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qt-wrappers.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/display-helpers.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/obs-app.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/obs-app.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/api-interface.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform-windows.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/src/window-basic-main-profiles.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/window-basic-main-outputs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/window-basic-main-outputs.hpp

  ${CMAKE_CURRENT_SOURCE_DIR}/src/audio-encoders.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/audio-encoders.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/src/win-dll-blocklist.c

  ${CMAKE_CURRENT_SOURCE_DIR}/src/obs-source-manager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/obs-source-manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/obs-sources.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/obs-sources.cpp
)

find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil avformat)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE

  FFmpeg::avcodec
  FFmpeg::avutil
  FFmpeg::avformat
  OBS::libobs
  OBS::frontend-api
  OBS::json11
  OBS::w32-pthreads
  Detours::Detours
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

foreach(graphics_library IN ITEMS opengl metal d3d11)
  string(TOUPPER ${graphics_library} graphics_library_U)
  if(TARGET OBS::libobs-${graphics_library})
    target_compile_definitions(${PROJECT_NAME}
                               PRIVATE DL_${graphics_library_U}="$<TARGET_FILE_NAME:OBS::libobs-${graphics_library}>")
  else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE DL_${graphics_library_U}="")
  endif()
endforeach()

get_property(obs_module_list GLOBAL PROPERTY OBS_MODULES_ENABLED)
list(JOIN obs_module_list "|" SAFE_MODULES)
target_compile_definitions(${PROJECT_NAME} PRIVATE "SAFE_MODULES=\"${SAFE_MODULES}\"")

if(CMAKE_COMPILE_WARNING_AS_ERROR)
  add_link_options(/WX-)
endif()

# cmake-format: off
set_target_properties_obs(${PROJECT_NAME} PROPERTIES FOLDER app OUTPUT_NAME "$<IF:$<PLATFORM_ID:Windows>,recorder64,recorder>")
# cmake-format: on
