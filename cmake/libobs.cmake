set(OBS_ROOT "" CACHE PATH "Pinned minimal OBS 32.1.2 SDK/runtime from tools/prepare-obs.ps1")
if(NOT EXISTS "${OBS_ROOT}/pin.json")
  message(FATAL_ERROR "Prepare OBS with tools/prepare-obs.ps1 and set OBS_ROOT.")
endif()
file(READ "${OBS_ROOT}/pin.json" obs_pin)
string(JSON obs_version GET "${obs_pin}" version)
if(NOT obs_version STREQUAL "32.1.2")
  message(FATAL_ERROR "Only the pinned OBS 32.1.2 runtime is supported.")
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/obsconfig.h.in" "${CMAKE_CURRENT_BINARY_DIR}/obs-include/obsconfig.h" @ONLY)
add_library(OBS::libobs SHARED IMPORTED)
set_target_properties(OBS::libobs PROPERTIES
  IMPORTED_IMPLIB "${OBS_ROOT}/lib/libobs.dll.a"
  IMPORTED_LOCATION "${OBS_ROOT}/bin/64bit/obs.dll"
  INTERFACE_INCLUDE_DIRECTORIES "${OBS_ROOT}/include/libobs;${CMAKE_CURRENT_BINARY_DIR}/obs-include")
target_compile_definitions(klip_core PUBLIC KLIP_USE_LIBOBS)
add_library(klip_media STATIC
  src/obs/obs_engine.cpp src/core/error.cpp src/core/logger.cpp
  src/graphics/d3d_device.cpp src/platform/hotkeys.cpp src/platform/user_paths.cpp
  src/platform/win32_window.cpp src/ui/imgui_host.cpp src/ui/main_panel.cpp)
target_include_directories(klip_media PUBLIC include)
target_compile_definitions(klip_media PUBLIC _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN WINRT_LEAN_AND_MEAN UNICODE _UNICODE NOMINMAX)
target_link_libraries(klip_media PUBLIC klip_core PRIVATE OBS::libobs imgui::imgui
  d3d11 dxgi dwmapi gdi32 imm32 ole32 shell32 uuid user32 windowscodecs windowsapp runtimeobject psapi)
klip_project_warnings(klip_media)
if(KLIP_ENABLE_TEST_HOOKS)
  target_compile_definitions(klip_media PUBLIC KLIP_ENABLE_TEST_HOOKS)
endif()
add_executable(klip_app WIN32 src/app/application_obs.cpp src/main.cpp)
set(KLIP_ICON_RESOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/klip.ico")
configure_file(packaging/klip.rc.in "${CMAKE_CURRENT_BINARY_DIR}/klip.rc" @ONLY)
target_sources(klip_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/klip.rc")
set_target_properties(klip_app PROPERTIES OUTPUT_NAME Klip RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/64bit")
target_link_libraries(klip_app PRIVATE klip_media imgui::imgui)
if(MINGW)
  target_link_options(klip_app PRIVATE -municode)
endif()
klip_project_warnings(klip_app)
if(KLIP_BUILD_TESTS)
  add_executable(klip_output_reservation_tests tests/output_reservation_tests.cpp)
  target_include_directories(klip_output_reservation_tests PRIVATE include)
  target_compile_definitions(klip_output_reservation_tests PRIVATE UNICODE _UNICODE NOMINMAX)
  klip_project_warnings(klip_output_reservation_tests)
  add_test(NAME klip_output_reservation_tests COMMAND klip_output_reservation_tests)
  set_tests_properties(klip_output_reservation_tests PROPERTIES TIMEOUT 30)
  add_executable(klip_event_driven_tabs_tests tests/event_driven_tabs_tests.cpp)
  target_include_directories(klip_event_driven_tabs_tests PRIVATE include)
  target_link_libraries(klip_event_driven_tabs_tests PRIVATE imgui::imgui user32 imm32)
  klip_project_warnings(klip_event_driven_tabs_tests)
  add_test(NAME klip_event_driven_tabs_tests COMMAND klip_event_driven_tabs_tests)
  set_tests_properties(klip_event_driven_tabs_tests PROPERTIES TIMEOUT 30)
endif()
add_custom_command(TARGET klip_app POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${OBS_ROOT}/bin" "${CMAKE_CURRENT_BINARY_DIR}/bin"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${OBS_ROOT}/obs-plugins" "${CMAKE_CURRENT_BINARY_DIR}/obs-plugins"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${OBS_ROOT}/data" "${CMAKE_CURRENT_BINARY_DIR}/data"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${OBS_ROOT}/licenses" "${CMAKE_CURRENT_BINARY_DIR}/licenses"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OBS_ROOT}/pin.json" "${CMAKE_CURRENT_BINARY_DIR}/obs-pin.json"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts" "$<TARGET_FILE_DIR:klip_app>/fonts")
