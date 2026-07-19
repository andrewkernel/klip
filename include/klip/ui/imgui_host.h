#pragma once

#include <Windows.h>
#include <d3d11.h>

#include "klip/core/error.h"

namespace klip {

class ImGuiHost {
 public:
  ImGuiHost() = default;
  ~ImGuiHost() noexcept;
  ImGuiHost(const ImGuiHost&) = delete;
  ImGuiHost& operator=(const ImGuiHost&) = delete;

  bool Initialize(HWND window, ID3D11Device* device, ID3D11DeviceContext* context, Error& error);
  void Shutdown() noexcept;
  void BeginFrame();
  void Render();

 private:
  bool win32_initialized_ = false;
  bool dx11_initialized_ = false;
};

}  // namespace klip
