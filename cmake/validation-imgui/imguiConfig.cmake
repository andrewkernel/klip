# Local validation adapter for the pinned upstream ImGui sources.
if(NOT TARGET imgui::imgui)
  add_library(klip_validation_imgui STATIC
    "${KLIP_IMGUI_SOURCE}/imgui.cpp"
    "${KLIP_IMGUI_SOURCE}/imgui_draw.cpp"
    "${KLIP_IMGUI_SOURCE}/imgui_tables.cpp"
    "${KLIP_IMGUI_SOURCE}/imgui_widgets.cpp"
    "${KLIP_IMGUI_SOURCE}/backends/imgui_impl_dx11.cpp"
    "${KLIP_IMGUI_SOURCE}/backends/imgui_impl_win32.cpp")
  target_include_directories(klip_validation_imgui PUBLIC
    "${KLIP_IMGUI_SOURCE}" "${KLIP_IMGUI_SOURCE}/backends")
  target_link_libraries(klip_validation_imgui PUBLIC d3dcompiler dwmapi)
  add_library(imgui::imgui ALIAS klip_validation_imgui)
endif()
