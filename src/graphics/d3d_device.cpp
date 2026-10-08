#include "klip/graphics/d3d_device.h"

#include <array>
#include <sstream>

namespace klip {

D3dDevice::~D3dDevice() noexcept { Shutdown(); }

bool D3dDevice::Initialize(HWND window, Error& error,
                           std::optional<std::uint32_t> required_vendor_id,
                           std::optional<D3D_FEATURE_LEVEL> requested_feature_level) {
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
  constexpr std::array levels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                              D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  const auto create_device = [&](IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver_type) {
    render_target_ = nullptr;
    swap_chain_ = nullptr;
    context_ = nullptr;
    device_ = nullptr;
    if (requested_feature_level) {
      return D3D11CreateDeviceAndSwapChain(adapter, driver_type, nullptr, flags,
                                           &requested_feature_level.value(), 1, D3D11_SDK_VERSION,
                                           &swap_desc, swap_chain_.put(), device_.put(),
                                           &feature_level_, context_.put());
    }
    return D3D11CreateDeviceAndSwapChain(adapter, driver_type, nullptr, flags, levels.data(),
                                         static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
                                         &swap_desc, swap_chain_.put(), device_.put(),
                                         &feature_level_, context_.put());
  };

  HRESULT result = E_FAIL;
  if (required_vendor_id.has_value()) {
    winrt::com_ptr<IDXGIFactory1> factory;
    result = CreateDXGIFactory1(IID_PPV_ARGS(factory.put()));
    if (FAILED(result)) {
      error =
          MakeHresultError(ErrorComponent::kGraphics, "enumerate requested D3D11 adapter", result);
      return false;
    }

    bool found_adapter = false;
    std::ostringstream attempts;
    for (UINT index = 0;; ++index) {
      winrt::com_ptr<IDXGIAdapter1> adapter;
      result = factory->EnumAdapters1(index, adapter.put());
      if (result == DXGI_ERROR_NOT_FOUND) break;
      if (FAILED(result)) {
        error = MakeHresultError(ErrorComponent::kGraphics, "enumerate requested D3D11 adapter",
                                 result);
        return false;
      }
      DXGI_ADAPTER_DESC1 description{};
      if (FAILED(adapter->GetDesc1(&description)) ||
          (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
          description.VendorId != *required_vendor_id)
        continue;

      found_adapter = true;
      result = create_device(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN);
      if (FAILED(result)) {
        if (attempts.tellp() > 0) attempts << "; ";
        attempts << "adapter " << index << " failed (HRESULT 0x" << std::hex
                 << static_cast<unsigned long>(result) << ')';
        continue;
      }
      break;
    }
    if (!found_adapter) {
      error = Error{ErrorComponent::kGraphics,
                    "select D3D11 adapter",
                    "no installed hardware adapter matches the requested vendor ID",
                    {},
                    {},
                    std::to_string(*required_vendor_id)};
      return false;
    }
    if (FAILED(result)) {
      error = MakeHresultError(ErrorComponent::kGraphics,
                               "initialize requested D3D11 hardware adapter", result);
      error.message = "Klip could not initialize a matching Direct3D 11 hardware adapter";
      error.context = attempts.str();
      return false;
    }
  } else {
    result = create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE);
  }

  if (!required_vendor_id.has_value() && FAILED(result)) {
    const auto default_adapter_result = result;
    std::ostringstream attempts;
    attempts << "default adapter failed (HRESULT 0x" << std::hex
             << static_cast<unsigned long>(default_adapter_result) << ')';

    winrt::com_ptr<IDXGIFactory1> factory;
    const auto factory_result = CreateDXGIFactory1(IID_PPV_ARGS(factory.put()));
    if (SUCCEEDED(factory_result)) {
      for (UINT index = 0;; ++index) {
        winrt::com_ptr<IDXGIAdapter1> adapter;
        const auto enumerate_result = factory->EnumAdapters1(index, adapter.put());
        if (enumerate_result == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(enumerate_result)) {
          attempts << "; adapter enumeration failed (HRESULT 0x" << std::hex
                   << static_cast<unsigned long>(enumerate_result) << ')';
          break;
        }

        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
          continue;

        result = create_device(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN);
        if (SUCCEEDED(result)) break;
        attempts << "; adapter " << index << " failed (HRESULT 0x" << std::hex
                 << static_cast<unsigned long>(result) << ')';
      }
    } else {
      attempts << "; adapter enumeration unavailable (HRESULT 0x" << std::hex
               << static_cast<unsigned long>(factory_result) << ')';
    }

    if (FAILED(result)) {
      error = MakeHresultError(ErrorComponent::kGraphics, "initialize D3D11 hardware adapter",
                               default_adapter_result);
      error.message = "Klip could not initialize a Direct3D 11 hardware adapter";
      error.context = attempts.str();
      return false;
    }
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
  feature_level_ = static_cast<D3D_FEATURE_LEVEL>(0);
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
