cmake_minimum_required(VERSION 3.24)
project(Klip VERSION 2.0.0 LANGUAGES CXX)

option(KLIP_BUILD_TESTS "Build deterministic Klip core tests" ON)
option(KLIP_BUILD_APP "Build the Windows capture application" ON)
option(KLIP_ENABLE_TEST_HOOKS "Enable test-only runtime fault injection" OFF)
option(KLIP_USE_LIBOBS "Use OBS capture, encoding, audio and replay output" ON)
set(FFMPEG_ROOT "" CACHE PATH "Optional FFmpeg root containing include, lib, and bin")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Threads REQUIRED)

function(klip_project_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_library(klip_core STATIC
  src/core/audio_timeline.cpp
  src/core/application_state.cpp
  src/core/clip_selection.cpp
  src/core/config.cpp
  src/core/config_store.cpp
  src/core/encoder_tuning.cpp
  src/core/encoder_selection.cpp
  src/core/video_timeline.cpp)
target_include_directories(klip_core PUBLIC include)
target_compile_features(klip_core PUBLIC cxx_std_20)
target_link_libraries(klip_core PUBLIC Threads::Threads)
klip_project_warnings(klip_core)

if(KLIP_BUILD_TESTS)
  include(CTest)
  add_executable(klip_tests tests/core_tests.cpp)
  target_link_libraries(klip_tests PRIVATE klip_core)
  klip_project_warnings(klip_tests)
  add_test(NAME klip_core_tests COMMAND klip_tests)
  set_tests_properties(klip_core_tests PROPERTIES TIMEOUT 30)
endif()

if(NOT WIN32 OR NOT KLIP_BUILD_APP)
  message(STATUS "Klip application target disabled or unavailable; core tests remain available.")
  return()
endif()

enable_language(RC)

find_package(imgui CONFIG REQUIRED)

if(KLIP_USE_LIBOBS)
  include(cmake/libobs.cmake)
  return()
endif()

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
  HINTS "${FFMPEG_ROOT}"
  PATH_SUFFIXES include)
foreach(component IN ITEMS avcodec avformat avutil swresample)
  string(TOUPPER "${component}" component_upper)
  find_library(FFMPEG_${component_upper}_LIBRARY
    NAMES "${component}"
    HINTS "${FFMPEG_ROOT}"
    PATH_SUFFIXES lib)
  if(NOT FFMPEG_${component_upper}_LIBRARY)
    message(FATAL_ERROR "Could not find FFmpeg ${component}; set FFMPEG_ROOT or use a vcpkg toolchain.")
  endif()
endforeach()
if(NOT FFMPEG_INCLUDE_DIR)
  message(FATAL_ERROR "Could not find FFmpeg headers; set FFMPEG_ROOT or use a vcpkg toolchain.")
endif()

add_library(klip_media STATIC
  src/audio/audio_pipeline.cpp
  src/buffer/rolling_media_buffer.cpp
  src/capture/graphics_capture.cpp
  src/clips/clip_writer.cpp
  src/core/error.cpp
  src/core/logger.cpp
  src/encoding/audio_encoder.cpp
  src/encoding/video_encoder.cpp
  src/graphics/d3d_device.cpp
  src/media/packet.cpp
  src/media/mp4_muxer.cpp
  src/media/packet_router.cpp
  src/platform/hotkeys.cpp
  src/platform/user_paths.cpp
  src/recording/recording_writer.cpp
  src/platform/win32_window.cpp
  src/ui/imgui_host.cpp
  src/ui/main_panel.cpp)
target_include_directories(klip_media
  PUBLIC include
  PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_compile_definitions(klip_media PUBLIC
  _WIN32_WINNT=0x0A00
  WIN32_LEAN_AND_MEAN
  WINRT_LEAN_AND_MEAN
  UNICODE
  _UNICODE
  NOMINMAX)
if(KLIP_ENABLE_TEST_HOOKS)
  target_compile_definitions(klip_media PUBLIC KLIP_ENABLE_TEST_HOOKS)
endif()
target_link_libraries(klip_media
  PUBLIC klip_core
  PRIVATE
    imgui::imgui
    "${FFMPEG_AVCODEC_LIBRARY}"
    "${FFMPEG_AVFORMAT_LIBRARY}"
    "${FFMPEG_AVUTIL_LIBRARY}"
    "${FFMPEG_SWRESAMPLE_LIBRARY}"
    avrt
    d3d11
    dxgi
    dwmapi
    gdi32
    imm32
    ksuser
    mmdevapi
    ole32
    propsys
    shell32
    uuid
    user32
    windowscodecs
    windowsapp
    runtimeobject)
klip_project_warnings(klip_media)

add_executable(klip_app WIN32
  src/app/application.cpp
  src/main.cpp)
set(KLIP_ICON_RESOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/klip.ico")
configure_file(packaging/klip.rc.in "${CMAKE_CURRENT_BINARY_DIR}/klip.rc" @ONLY)
target_sources(klip_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/klip.rc")
set_target_properties(klip_app PROPERTIES OUTPUT_NAME Klip)
if(KLIP_BUILD_TESTS)
  add_executable(klip_windows_tests tests/windows_tests.cpp)
  target_include_directories(klip_windows_tests PRIVATE "${FFMPEG_INCLUDE_DIR}")
  target_link_libraries(klip_windows_tests PRIVATE klip_media "${FFMPEG_AVCODEC_LIBRARY}"
    "${FFMPEG_AVFORMAT_LIBRARY}")
  add_test(NAME klip_windows_tests COMMAND klip_windows_tests)
  set_tests_properties(klip_windows_tests PROPERTIES TIMEOUT 30)
endif()
if(MINGW)
  target_link_options(klip_app PRIVATE -municode)
endif()
target_include_directories(klip_app PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_link_libraries(klip_app PRIVATE
  klip_media
  imgui::imgui
  "${FFMPEG_AVUTIL_LIBRARY}")
klip_project_warnings(klip_app)

set(KLIP_FONT_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Medium.ttf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-SemiBold.ttf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Bold.ttf")
add_custom_command(TARGET klip_app POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:klip_app>/fonts"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          ${KLIP_FONT_FILES} "$<TARGET_FILE_DIR:klip_app>/fonts"
  COMMAND_EXPAND_LISTS)

if(FFMPEG_ROOT)
  file(GLOB FFMPEG_RUNTIME_DLLS CONFIGURE_DEPENDS
    "${FFMPEG_ROOT}/bin/avcodec-*.dll"
    "${FFMPEG_ROOT}/bin/avformat-*.dll"
    "${FFMPEG_ROOT}/bin/avutil-*.dll"
    "${FFMPEG_ROOT}/bin/swresample-*.dll")
  if(FFMPEG_RUNTIME_DLLS)
    add_custom_command(TARGET klip_app POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              ${FFMPEG_RUNTIME_DLLS} "$<TARGET_FILE_DIR:klip_app>"
      COMMAND_EXPAND_LISTS)
  endif()
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include" PREFIX "Headers"
  FILES
    include/klip/app/application.h
    include/klip/audio/audio_pipeline.h
    include/klip/buffer/rolling_media_buffer.h
    include/klip/capture/graphics_capture.h
    include/klip/clips/clip_writer.h
    include/klip/core/application_state.h
    include/klip/core/audio_timeline.h
    include/klip/core/bounded_queue.h
    include/klip/core/clip_selection.h
    include/klip/core/config.h
    include/klip/core/config_store.h
    include/klip/core/encoder_selection.h
    include/klip/core/error.h
    include/klip/core/logger.h
    include/klip/encoding/audio_encoder.h
    include/klip/encoding/video_encoder.h
    include/klip/graphics/d3d_device.h
    include/klip/media/packet.h
    include/klip/media/mp4_muxer.h
    include/klip/media/packet_router.h
    include/klip/platform/hotkeys.h
    include/klip/platform/user_paths.h
    include/klip/recording/recording_writer.h
    include/klip/platform/scoped_handle.h
    include/klip/platform/win32_window.h
    include/klip/ui/imgui_host.h
    include/klip/ui/main_panel.h)
