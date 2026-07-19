#pragma once

#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include <cstdint>
#include <string>

#include "klip/core/error.h"

namespace klip {

class D3dDevice {
 public:
  D3dDevice() = default;
  ~D3dDevice() noexcept;

  D3dDevice(const D3dDevice&) = delete;
  D3dDevice& operator=(const D3dDevice&) = delete;

  bool Initialize(HWND window, Error& error);
  void Shutdown() noexcept;
  bool Resize(UINT width, UINT height, Error& error);
  bool BeginFrame(const float clear_color[4]);
  void Present();

  [[nodiscard]] ID3D11Device* Device() const noexcept { return device_.get(); }
  [[nodiscard]] ID3D11DeviceContext* Context() const noexcept { return context_.get(); }
  [[nodiscard]] ID3D11RenderTargetView* RenderTarget() const noexcept {
    return render_target_.get();
  }
  [[nodiscard]] std::uint32_t AdapterVendorId() const noexcept { return adapter_vendor_id_; }
  [[nodiscard]] const std::wstring& AdapterName() const noexcept { return adapter_name_; }

 private:
  bool CreateRenderTarget(Error& error);

  winrt::com_ptr<ID3D11Device> device_;
  winrt::com_ptr<ID3D11DeviceContext> context_;
  winrt::com_ptr<IDXGISwapChain> swap_chain_;
  winrt::com_ptr<ID3D11RenderTargetView> render_target_;
  std::uint32_t adapter_vendor_id_ = 0;
  std::wstring adapter_name_;
};

}  // namespace klip
