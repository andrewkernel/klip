#include "klip/ui/imgui_host.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

namespace klip {
namespace {

void ApplyStyle() {
  ImGui::StyleColorsDark();
  auto& style = ImGui::GetStyle();
  style.WindowRounding = 14.0F;
  style.FrameRounding = 10.0F;
  style.GrabRounding = 10.0F;
  style.PopupRounding = 10.0F;
  style.ScrollbarRounding = 10.0F;
  style.WindowBorderSize = 0.0F;
  style.WindowPadding = ImVec2(24.0F, 20.0F);
  style.ItemSpacing = ImVec2(10.0F, 9.0F);
  style.FramePadding = ImVec2(12.0F, 9.0F);
  auto* colors = style.Colors;
  colors[ImGuiCol_WindowBg] = ImVec4(0.04F, 0.06F, 0.09F, 1.0F);
  colors[ImGuiCol_PopupBg] = ImVec4(0.06F, 0.08F, 0.12F, 0.99F);
  colors[ImGuiCol_Border] = ImVec4(0.18F, 0.21F, 0.27F, 1.0F);
  colors[ImGuiCol_TitleBg] = ImVec4(0.04F, 0.06F, 0.09F, 1.0F);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.06F, 0.08F, 0.12F, 1.0F);
  colors[ImGuiCol_Button] = ImVec4(0.40F, 0.17F, 0.72F, 1.0F);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.52F, 0.24F, 0.88F, 1.0F);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.34F, 0.13F, 0.64F, 1.0F);
  colors[ImGuiCol_FrameBg] = ImVec4(0.08F, 0.11F, 0.15F, 1.0F);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.11F, 0.14F, 0.19F, 1.0F);
  colors[ImGuiCol_Header] = ImVec4(0.35F, 0.14F, 0.62F, 0.75F);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.48F, 0.21F, 0.82F, 0.90F);
  colors[ImGuiCol_CheckMark] = ImVec4(0.70F, 0.34F, 1.0F, 1.0F);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.64F, 0.29F, 0.96F, 1.0F);
  colors[ImGuiCol_PlotHistogram] = ImVec4(0.69F, 0.34F, 1.0F, 1.0F);
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
  io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 42.0F, &font_config);
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
