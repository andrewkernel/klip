#include "klip/graphics/d3d_device.h"

#include <array>

namespace klip {

D3dDevice::~D3dDevice() noexcept { Shutdown(); }

bool D3dDevice::Initialize(HWND window, Error& error) {
  Shutdown();
  DXGI_SWAP_CHAIN_DESC swap_desc{};
  swap_desc.BufferCount = 2;
  swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_desc.OutputWindow = window;
  swap_desc.SampleDesc.Count = 1;
  swap_desc.Windowed = TRUE;
  swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
  constexpr std::array levels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL created_level{};
  const auto result = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels.data(),
      static_cast<UINT>(levels.size()), D3D11_SDK_VERSION, &swap_desc, swap_chain_.put(),
      device_.put(), &created_level, context_.put());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kGraphics, "D3D11CreateDeviceAndSwapChain", result);
    return false;
  }

  winrt::com_ptr<ID3D11Multithread> multithread;
  if (FAILED(context_->QueryInterface(IID_PPV_ARGS(multithread.put())))) {
    error = Error{ErrorComponent::kGraphics, "enable multithread protection",
                  "ID3D11Multithread is unavailable"};
    return false;
  }
  multithread->SetMultithreadProtected(TRUE);

  winrt::com_ptr<IDXGIDevice> dxgi_device;
  winrt::com_ptr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC adapter_desc{};
  if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(dxgi_device.put()))) &&
      SUCCEEDED(dxgi_device->GetAdapter(adapter.put())) &&
      SUCCEEDED(adapter->GetDesc(&adapter_desc))) {
    adapter_vendor_id_ = adapter_desc.VendorId;
    adapter_name_ = adapter_desc.Description;
  }
  return CreateRenderTarget(error);
}

void D3dDevice::Shutdown() noexcept {
  render_target_ = nullptr;
  swap_chain_ = nullptr;
  context_ = nullptr;
  device_ = nullptr;
  adapter_vendor_id_ = 0;
  adapter_name_.clear();
}

bool D3dDevice::Resize(UINT width, UINT height, Error& error) {
  if (swap_chain_ == nullptr || width == 0 || height == 0) {
    return false;
  }
  render_target_ = nullptr;
  const auto result = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kGraphics, "resize swap chain", result);
    return false;
  }
  return CreateRenderTarget(error);
}

bool D3dDevice::BeginFrame(const float clear_color[4]) {
  if (context_ == nullptr || render_target_ == nullptr) {
    return false;
  }
  auto* target = render_target_.get();
  context_->OMSetRenderTargets(1, &target, nullptr);
  context_->ClearRenderTargetView(target, clear_color);
  return true;
}

void D3dDevice::Present() {
  if (swap_chain_ != nullptr) {
    swap_chain_->Present(1, 0);
  }
}

bool D3dDevice::CreateRenderTarget(Error& error) {
  winrt::com_ptr<ID3D11Texture2D> back_buffer;
  auto result = swap_chain_->GetBuffer(0, IID_PPV_ARGS(back_buffer.put()));
  if (SUCCEEDED(result)) {
    result = device_->CreateRenderTargetView(back_buffer.get(), nullptr, render_target_.put());
  }
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kGraphics, "create swap-chain render target", result);
    return false;
  }
  return true;
}

}  // namespace klip
