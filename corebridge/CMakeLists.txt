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

# set target name
set(TARGET_NAME corebridge)
add_library(${TARGET_NAME} SHARED)
add_library(OBS::corebridge ALIAS corebridge)

find_package(Detours REQUIRED)

# Qt
find_qt(COMPONENTS Widgets Core)
target_link_libraries(${TARGET_NAME} PRIVATE Qt::Widgets Qt::Core)
set_target_properties(${TARGET_NAME} PROPERTIES AUTOMOC ON)

# FFmpeg
find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil avformat)

target_link_libraries(
  ${TARGET_NAME}
  PRIVATE

  FFmpeg::avcodec
  FFmpeg::avutil
  FFmpeg::avformat
)

target_sources(
  ${TARGET_NAME}
  PRIVATE

  # core app
  ${CMAKE_CURRENT_SOURCE_DIR}/app.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/app.h
  ${CMAKE_CURRENT_SOURCE_DIR}/app-profile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/utils.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ui.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ui.h
  ${CMAKE_CURRENT_SOURCE_DIR}/output.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/output.h
  ${CMAKE_CURRENT_SOURCE_DIR}/preview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/preview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/scene-source.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scene-source.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source-preview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source-preview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/defines.h
  ${CMAKE_CURRENT_SOURCE_DIR}/qt-display.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/qt-display.h
  ${CMAKE_CURRENT_SOURCE_DIR}/display-helpers.h
  # ${CMAKE_CURRENT_SOURCE_DIR}/core/api-interface.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/platform.h
  ${CMAKE_CURRENT_SOURCE_DIR}/platform-windows.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/audio-encoders.h
  ${CMAKE_CURRENT_SOURCE_DIR}/audio-encoders.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/win-dll-blocklist.c
)

target_link_libraries(
  ${TARGET_NAME}
  PRIVATE

  OBS::libobs
  OBS::frontend-api
  OBS::json11
  OBS::w32-pthreads
  Detours::Detours
)

set_property(
  TARGET ${TARGET_NAME}
  APPEND
  PROPERTY AUTORCC_OPTIONS --format-version 1)

target_link_options(${TARGET_NAME} PRIVATE /IGNORE:4098 /IGNORE:4099)

foreach(graphics_library IN ITEMS opengl metal d3d11)
  string(TOUPPER ${graphics_library} graphics_library_U)
  if(TARGET OBS::libobs-${graphics_library})
    target_compile_definitions(${TARGET_NAME}
                               PRIVATE DL_${graphics_library_U}="$<TARGET_FILE_NAME:OBS::libobs-${graphics_library}>")
  else()
    target_compile_definitions(${TARGET_NAME} PRIVATE DL_${graphics_library_U}="")
  endif()
endforeach()

get_property(obs_module_list GLOBAL PROPERTY OBS_MODULES_ENABLED)
list(JOIN obs_module_list "|" SAFE_MODULES)
target_compile_definitions(${TARGET_NAME} PRIVATE "SAFE_MODULES=\"${SAFE_MODULES}\"")

if(CMAKE_COMPILE_WARNING_AS_ERROR)
  add_link_options(/WX-)
endif()

# cmake-format: off
set_target_properties_obs(${TARGET_NAME} PROPERTIES FOLDER app/core OUTPUT_NAME corebridge)
# cmake-format: on
