if(NOT TARGET OBS::w32-pthreads)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/w32-pthreads" "${CMAKE_BINARY_DIR}/deps/w32-pthreads")
endif()

if(NOT TARGET OBS::frontend-api)
  add_subdirectory("${CMAKE_SOURCE_DIR}/UI/obs-frontend-api" "${CMAKE_BINARY_DIR}/UI/obs-frontend-api")
endif()

if(NOT TARGET OBS::json11)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/json11" "${CMAKE_BINARY_DIR}/deps/json11")
endif()

find_package(Detours REQUIRED)

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
  ${CMAKE_CURRENT_SOURCE_DIR}/core/source-preview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/source-preview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/defines.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/qt-display.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/qt-display.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/display-helpers.h
  # ${CMAKE_CURRENT_SOURCE_DIR}/core/api-interface.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/platform.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/platform-windows.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/audio-encoders.h
  ${CMAKE_CURRENT_SOURCE_DIR}/core/audio-encoders.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/win-dll-blocklist.c
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

target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0601)

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
set_target_properties_obs(${PROJECT_NAME} PROPERTIES FOLDER app OUTPUT_NAME "$<IF:$<PLATFORM_ID:Windows>,${PROJECT_NAME}64,${PROJECT_NAME}>")
# cmake-format: on

# copy locale file to OBS_DATA_PATH
file(COPY ${CMAKE_SOURCE_DIR}/UI/data/locale
     DESTINATION ${OBS_DATA_PATH}/${PROJECT_NAME})
file(COPY ${CMAKE_SOURCE_DIR}/UI/data/locale.ini
     DESTINATION ${OBS_DATA_PATH}/${PROJECT_NAME})
