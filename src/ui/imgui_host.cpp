#include "klip/ui/imgui_host.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

namespace klip {
namespace {

void ApplyStyle() {
  ImGui::StyleColorsDark();
  auto& style = ImGui::GetStyle();
  style.WindowRounding = 10.0F;
  style.FrameRounding = 7.0F;
  style.GrabRounding = 7.0F;
  style.PopupRounding = 8.0F;
  style.ScrollbarRounding = 8.0F;
  style.WindowBorderSize = 0.0F;
  style.WindowPadding = ImVec2(24.0F, 20.0F);
  style.ItemSpacing = ImVec2(11.0F, 10.0F);
  style.FramePadding = ImVec2(12.0F, 8.0F);
  auto* colors = style.Colors;
  colors[ImGuiCol_Text] = ImVec4(0.94F, 0.93F, 0.91F, 1.0F);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.46F, 0.47F, 0.44F, 1.0F);
  colors[ImGuiCol_WindowBg] = ImVec4(0.035F, 0.043F, 0.039F, 1.0F);
  colors[ImGuiCol_PopupBg] = ImVec4(0.055F, 0.058F, 0.067F, 0.99F);
  colors[ImGuiCol_Border] = ImVec4(0.22F, 0.22F, 0.24F, 0.86F);
  colors[ImGuiCol_TitleBg] = ImVec4(0.035F, 0.043F, 0.039F, 1.0F);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.05F, 0.05F, 0.06F, 1.0F);
  colors[ImGuiCol_Button] = ImVec4(0.39F, 0.17F, 0.70F, 1.0F);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.51F, 0.25F, 0.86F, 1.0F);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.32F, 0.12F, 0.61F, 1.0F);
  colors[ImGuiCol_FrameBg] = ImVec4(0.075F, 0.078F, 0.09F, 1.0F);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.105F, 0.108F, 0.12F, 1.0F);
  colors[ImGuiCol_Header] = ImVec4(0.34F, 0.14F, 0.61F, 0.72F);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.48F, 0.22F, 0.80F, 0.90F);
  colors[ImGuiCol_CheckMark] = ImVec4(0.72F, 0.46F, 1.0F, 1.0F);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.68F, 0.40F, 0.97F, 1.0F);
  colors[ImGuiCol_PlotHistogram] = ImVec4(0.72F, 0.43F, 1.0F, 1.0F);
}

}  // namespace

ImGuiHost::~ImGuiHost() noexcept { Shutdown(); }

bool ImGuiHost::Initialize(HWND window, ID3D11Device* device, ID3D11DeviceContext* context,
                           Error& error) {
  Shutdown();
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  ImFontConfig font_config{};
  font_config.OversampleH = 2;
  font_config.OversampleV = 2;
  font_config.PixelSnapH = false;
  auto* regular = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0F,
                                               &font_config);
  io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 17.0F, &font_config);
  io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 30.0F, &font_config);
  io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuil.ttf", 44.0F, &font_config);
  io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 13.0F, &font_config);
  if (regular == nullptr) io.Fonts->AddFontDefault();
  io.FontDefault = regular;
  ApplyStyle();
  win32_initialized_ = ImGui_ImplWin32_Init(window);
  dx11_initialized_ = win32_initialized_ && ImGui_ImplDX11_Init(device, context);
  if (!win32_initialized_ || !dx11_initialized_) {
    error = Error{ErrorComponent::kWindow, "initialize ImGui",
                  "the Win32 or D3D11 backend failed to initialize"};
    Shutdown();
    return false;
  }
  return true;
}

void ImGuiHost::Shutdown() noexcept {
  if (dx11_initialized_) ImGui_ImplDX11_Shutdown();
  if (win32_initialized_) ImGui_ImplWin32_Shutdown();
  if (ImGui::GetCurrentContext() != nullptr) ImGui::DestroyContext();
  dx11_initialized_ = false;
  win32_initialized_ = false;
}

void ImGuiHost::BeginFrame() {
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
}

void ImGuiHost::Render() {
  ImGui::Render();
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace klip
