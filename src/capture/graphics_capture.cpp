#include "klip/capture/graphics_capture.h"

#include "klip/core/video_timeline.h"

#include <dwmapi.h>
#include <wincodec.h>
// MinGW's WinRT ABI headers typedef both BYTE and boolean to unsigned char, which makes their
// IReference specializations collide in C++. C++/WinRT uses bool for the WinRT Boolean ABI.
// MSVC's Windows SDK declares ABI::Windows::Foundation::boolean as a real type, so replacing it
// there would produce the invalid qualified name ABI::Windows::Foundation::bool.
#if defined(__MINGW32__) || defined(__MINGW64__)
#define boolean bool
#endif
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#if defined(__MINGW32__) || defined(__MINGW64__)
#undef boolean
#endif

#include <algorithm>
#include <cmath>
#include <exception>
#include <sstream>
#include <utility>

namespace klip {
namespace {

bool IsCloaked(HWND window) {
  DWORD cloaked = 0;
  return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
         cloaked != 0;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const auto required =
      WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (required <= 0) return {};
  std::string output(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), required, nullptr, nullptr);
  output.pop_back();
  return output;
}

bool MatchesPreferredWindow(const std::string& label, const std::string& preferred) {
  if (label == preferred) return true;
  const auto executable = preferred.rfind("  [");
  return executable != std::string::npos && label.ends_with(preferred.substr(executable));
}

}  // namespace

GraphicsCapture::GraphicsCapture(VideoEncoder& encoder, ApplicationState& state, Logger& logger)
    : encoder_(encoder), state_(state), logger_(logger) {}

GraphicsCapture::~GraphicsCapture() noexcept { Stop(); }

bool GraphicsCapture::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                                 HWND application_window, const AppConfig& config, Error& error) {
  Stop();
  if (device == nullptr || context == nullptr) {
    error = Error{ErrorComponent::kCapture, "initialize", "D3D11 device and context are required"};
    return false;
  }
  if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
    error = Error{ErrorComponent::kCapture, "check support",
                  "Windows Graphics Capture is not supported"};
    return false;
  }
  config_ = config;
  preview_enabled_.store(config.capture_preview_enabled, std::memory_order_release);
  application_window_ = application_window;
  target_mode_.store(config.target_mode, std::memory_order_release);
  device_.copy_from(device);
  context_.copy_from(context);
  if (FAILED(device_->QueryInterface(IID_PPV_ARGS(device5_.put()))) ||
      FAILED(context_->QueryInterface(IID_PPV_ARGS(context4_.put()))) ||
      FAILED(device_->QueryInterface(IID_PPV_ARGS(video_device_.put()))) ||
      FAILED(context_->QueryInterface(IID_PPV_ARGS(video_context_.put())))) {
    error = Error{ErrorComponent::kCapture, "query D3D11 interfaces",
                  "D3D11.4 video interfaces are unavailable"};
    return false;
  }
  if (config_.static_overlay_enabled) {
    Error overlay_error;
    if (!LoadStaticOverlay(overlay_error)) logger_.Warning(overlay_error.ToString());
  }
  if (!CreateInteropDevice(error) || !CreateFence(error)) {
    return false;
  }
  fence_event_.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
  if (!fence_event_) {
    error = MakeWin32Error(ErrorComponent::kCapture, "create fence event", GetLastError());
    return false;
  }
  LARGE_INTEGER frequency{};
  LARGE_INTEGER origin{};
  if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&origin)) {
    error = MakeWin32Error(ErrorComponent::kCapture, "query performance counter", GetLastError());
    return false;
  }
  qpc_frequency_ = frequency.QuadPart;
  qpc_origin_ = origin.QuadPart;
  raw_queue_ = std::make_unique<SpscQueue<RawFrame>>(config.raw_frame_queue_capacity);
  encode_queue_ = std::make_unique<SpscQueue<ConvertedFrame>>(config.encode_queue_capacity);
  RefreshSourceOptions();
  if (config_.live_overlay_enabled) {
    Error overlay_error;
    if (!StartLiveOverlay(overlay_error)) logger_.Warning(overlay_error.ToString());
  }
  return true;
}

bool GraphicsCapture::Start(Error& error) {
  if (running_.exchange(true, std::memory_order_acq_rel)) {
    return true;
  }
  recycle_enabled_.store(true, std::memory_order_release);
  {
    std::scoped_lock lock(session_mutex_);
    auto target = DetermineTarget();
    if (target.kind == TargetKind::kNone) {
      state_.SetTarget(CaptureTargetMode::kGameWindow, "Choose a game or window", 0);
    } else if (!StartSessionLocked(target, error)) {
      running_.store(false, std::memory_order_release);
      recycle_enabled_.store(false, std::memory_order_release);
      return false;
    }
  }

  state_.SetStatus(active_target_.kind == TargetKind::kNone ? CaptureStatus::kIdle
                                                            : CaptureStatus::kBuffering,
                   active_target_.kind == TargetKind::kNone ? "Choose a capture source"
                                                            : "Capturing");
  processing_thread_ = std::jthread([this](std::stop_token token) { ProcessingLoop(token); });
  encoding_thread_ = std::jthread([this](std::stop_token token) { EncodingLoop(token); });
  target_thread_ = std::jthread([this](std::stop_token token) { TargetLoop(token); });
  logger_.Info("Graphics capture started");
  return true;
}

void GraphicsCapture::Stop() noexcept {
  running_.store(false, std::memory_order_release);
  target_thread_.request_stop();
  processing_thread_.request_stop();
  encoding_thread_.request_stop();
  target_thread_ = {};
  {
    std::scoped_lock lock(session_mutex_);
    StopSessionLocked();
  }
  StopLiveOverlay();
  processing_thread_ = {};
  encoding_thread_ = {};
  recycle_enabled_.store(false, std::memory_order_release);
  if (raw_queue_) raw_queue_->Clear();
  if (encode_queue_) encode_queue_->Clear();
  {
    std::scoped_lock lock(texture_pool_mutex_);
    texture_pool_.clear();
  }
  {
    std::scoped_lock lock(preview_mutex_);
    preview_view_ = nullptr;
    preview_texture_ = nullptr;
    preview_width_ = 0;
    preview_height_ = 0;
    static_overlay_view_ = nullptr;
    static_overlay_texture_ = nullptr;
    static_overlay_width_ = 0;
    static_overlay_height_ = 0;
    live_overlay_view_ = nullptr;
    live_overlay_texture_ = nullptr;
    live_overlay_width_ = 0;
    live_overlay_height_ = 0;
    next_live_overlay_update_ = {};
  }
  overlay_warning_logged_ = false;
  logger_.Info("Graphics capture stopped");
}

void GraphicsCapture::SetTargetMode(CaptureTargetMode mode) {
  target_mode_.store(mode, std::memory_order_release);
  target_dirty_.store(true, std::memory_order_release);
}

void GraphicsCapture::SelectTarget(CaptureTargetMode mode, std::uint64_t source_id) {
  target_mode_.store(mode, std::memory_order_release);
  if (mode == CaptureTargetMode::kDisplay)
    selected_monitor_.store(static_cast<std::uintptr_t>(source_id), std::memory_order_release);
  else
    selected_window_.store(static_cast<std::uintptr_t>(source_id), std::memory_order_release);
  target_dirty_.store(true, std::memory_order_release);
}

void GraphicsCapture::SetBorderRequired(bool required) {
  std::scoped_lock lock(session_mutex_);
  config_.capture_border = required;
  if (session_ == nullptr) return;
  try {
    session_.IsBorderRequired(required);
  } catch (const winrt::hresult_error&) {
    logger_.Warning("Capture highlight control is unavailable on this Windows build");
  }
}

void GraphicsCapture::SetPreviewEnabled(bool enabled) noexcept {
  preview_enabled_.store(enabled, std::memory_order_release);
  config_.capture_preview_enabled = enabled;
  if (!enabled) {
    std::scoped_lock lock(preview_mutex_);
    preview_view_ = nullptr;
    preview_texture_ = nullptr;
    preview_width_ = 0;
    preview_height_ = 0;
  }
  logger_.Info(enabled ? "Low-rate capture preview enabled" : "Capture preview disabled");
}

CapturePreview GraphicsCapture::Preview() const {
  std::scoped_lock lock(preview_mutex_);
  auto overlay = config_.static_overlay_enabled ? static_overlay_view_ : live_overlay_view_;
  return CapturePreview{preview_view_, std::move(overlay), preview_width_, preview_height_};
}

bool GraphicsCapture::CreateInteropDevice(Error& error) {
  winrt::com_ptr<IDXGIDevice> dxgi;
  auto result = device_->QueryInterface(IID_PPV_ARGS(dxgi.put()));
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "query IDXGIDevice", result);
    return false;
  }
  winrt::com_ptr<::IInspectable> inspectable;
  result = CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "create WinRT D3D11 device", result);
    return false;
  }
  interop_device_ =
      inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
  return true;
}

bool GraphicsCapture::CreateFence(Error& error) {
  const auto result = device5_->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.put()));
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "create D3D11 fence", result);
    return false;
  }
  return true;
}

GraphicsCapture::CaptureTarget GraphicsCapture::DetermineTarget() {
  if (target_mode_.load(std::memory_order_acquire) == CaptureTargetMode::kDisplay) {
    auto monitor = reinterpret_cast<HMONITOR>(selected_monitor_.load(std::memory_order_acquire));
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) {
      monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
      selected_monitor_.store(reinterpret_cast<std::uintptr_t>(monitor),
                              std::memory_order_release);
    }
    return {TargetKind::kMonitor, nullptr, monitor};
  }
  auto selected = reinterpret_cast<HWND>(selected_window_.load(std::memory_order_acquire));
  if (IsWindowCandidate(selected)) return {TargetKind::kWindow, selected, nullptr};
  return {};
}

void GraphicsCapture::RefreshSourceOptions() {
  std::vector<CaptureSourceOption> windows;
  std::pair<GraphicsCapture*, std::vector<CaptureSourceOption>*> window_context{this, &windows};
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto* pair = reinterpret_cast<std::pair<GraphicsCapture*,
                                                std::vector<CaptureSourceOption>*>*>(parameter);
        if (!pair->first->IsWindowCandidate(window)) return TRUE;
        const auto label = GraphicsCapture::WindowLabel(window);
        if (!label.empty())
          pair->second->push_back(
              {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window)), label});
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&window_context));

  std::sort(windows.begin(), windows.end(),
            [](const auto& left, const auto& right) { return left.label < right.label; });
  std::vector<CaptureSourceOption> displays;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR monitor, HDC, LPRECT, LPARAM parameter) -> BOOL {
        auto* options = reinterpret_cast<std::vector<CaptureSourceOption>*>(parameter);
        options->push_back(
            {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(monitor)),
             GraphicsCapture::MonitorLabel(monitor)});
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&displays));

  auto selected_window = selected_window_.load(std::memory_order_acquire);
  const auto window_match = [&](const CaptureSourceOption& option) {
    return option.id == selected_window;
  };
  if (selected_window != 0 && std::none_of(windows.begin(), windows.end(), window_match)) {
    selected_window = 0;
    selected_window_.store(0, std::memory_order_release);
  }
  if (selected_window == 0 && !config_.preferred_game_title.empty()) {
    const auto preferred = std::find_if(windows.begin(), windows.end(), [&](const auto& option) {
      return MatchesPreferredWindow(option.label, config_.preferred_game_title);
    });
    if (preferred != windows.end()) {
      selected_window = preferred->id;
      selected_window_.store(static_cast<std::uintptr_t>(preferred->id),
                             std::memory_order_release);
    }
  }

  auto selected_monitor = selected_monitor_.load(std::memory_order_acquire);
  const auto monitor_match = [&](const CaptureSourceOption& option) {
    return option.id == selected_monitor;
  };
  if (selected_monitor != 0 && std::none_of(displays.begin(), displays.end(), monitor_match)) {
    selected_monitor = 0;
    selected_monitor_.store(0, std::memory_order_release);
  }
  if (selected_monitor == 0 && !config_.preferred_display_name.empty()) {
    const auto preferred = std::find_if(displays.begin(), displays.end(), [&](const auto& option) {
      return option.label == config_.preferred_display_name;
    });
    if (preferred != displays.end()) {
      selected_monitor = preferred->id;
      selected_monitor_.store(static_cast<std::uintptr_t>(preferred->id),
                              std::memory_order_release);
    }
  }
  if (selected_monitor == 0 && !displays.empty()) {
    selected_monitor = displays.front().id;
    selected_monitor_.store(static_cast<std::uintptr_t>(selected_monitor),
                            std::memory_order_release);
  }

  const auto selected = target_mode_.load(std::memory_order_acquire) == CaptureTargetMode::kDisplay
                            ? selected_monitor
                            : selected_window;
  state_.SetCaptureSources(std::move(windows), std::move(displays), selected);
  last_source_refresh_ = std::chrono::steady_clock::now();
}

std::string GraphicsCapture::WindowLabel(HWND window) {
  const auto length = GetWindowTextLengthW(window);
  if (length <= 0) return {};
  std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
  const auto copied = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
  if (copied <= 0) return {};
  title.resize(static_cast<std::size_t>(copied));
  std::string label = WideToUtf8(title);
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
  if (process) {
    std::wstring executable(1024, L'\0');
    DWORD executable_size = static_cast<DWORD>(executable.size());
    if (QueryFullProcessImageNameW(process.Get(), 0, executable.data(), &executable_size)) {
      executable.resize(executable_size);
      const auto separator = executable.find_last_of(L"\\/");
      if (separator != std::wstring::npos) executable.erase(0, separator + 1);
      label += "  [" + WideToUtf8(executable) + ']';
    }
  }
  return label;
}

std::string GraphicsCapture::MonitorLabel(HMONITOR monitor) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) return "Display";
  const auto width = info.rcMonitor.right - info.rcMonitor.left;
  const auto height = info.rcMonitor.bottom - info.rcMonitor.top;
  std::ostringstream label;
  label << WideToUtf8(info.szDevice) << "  " << width << 'x' << height;
  if ((info.dwFlags & MONITORINFOF_PRIMARY) != 0) label << "  (Primary)";
  return label.str();
}

bool GraphicsCapture::IsWindowCandidate(HWND window) const {
  return window != nullptr && window != application_window_ && IsWindow(window) &&
         IsWindowVisible(window) && !IsIconic(window) && !IsCloaked(window);
}

bool GraphicsCapture::StartSessionLocked(const CaptureTarget& target, Error& error) {
  StopSessionLocked();
  last_enqueued_pts_.store(-1, std::memory_order_relaxed);
  try {
    item_ = target.kind == TargetKind::kWindow ? CreateForWindow(target.window)
                                               : CreateForMonitor(target.monitor);
    const auto size = item_.Size();
    if (size.Width <= 0 || size.Height <= 0) {
      error = Error{ErrorComponent::kCapture, "inspect capture target",
                    "capture target has an invalid size"};
      return false;
    }
    current_width_ = static_cast<std::uint32_t>(size.Width);
    current_height_ = static_cast<std::uint32_t>(size.Height);
    frame_pool_ = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
        interop_device_,
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, size);
    frame_token_ = frame_pool_.FrameArrived({this, &GraphicsCapture::OnFrameArrived});
    closed_token_ = item_.Closed({this, &GraphicsCapture::OnTargetClosed});
    session_ = frame_pool_.CreateCaptureSession(item_);
    try {
      session_.IsCursorCaptureEnabled(config_.capture_cursor);
    } catch (const winrt::hresult_error&) {
      logger_.Warning("Cursor capture control is unavailable on this Windows build");
    }
    try {
      session_.IsBorderRequired(config_.capture_border);
    } catch (const winrt::hresult_error&) {
      logger_.Warning("Capture highlight control is unavailable on this Windows build");
    }
    session_.StartCapture();
    active_target_ = target;
    const auto requested_mode = target_mode_.load(std::memory_order_acquire);
    const bool window = target.kind == TargetKind::kWindow;
    const auto source_id = window
                               ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target.window))
                               : static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target.monitor));
    const auto description = window
                                 ? WindowLabel(target.window)
                                 : (requested_mode == CaptureTargetMode::kGameWindow
                                        ? "Choose a game or window"
                                        : MonitorLabel(target.monitor));
    state_.SetTarget(requested_mode, description,
                     requested_mode == CaptureTargetMode::kGameWindow && !window ? 0 : source_id);
    state_.SetStatus(CaptureStatus::kBuffering, "Capturing");
    logger_.Info("Capture target: " + description);
    return true;
  } catch (const winrt::hresult_error& exception) {
    error =
        MakeHresultError(ErrorComponent::kCapture, "start capture session", exception.code().value);
    return false;
  } catch (const std::exception& exception) {
    error = Error{ErrorComponent::kCapture, "start capture session", exception.what()};
    return false;
  }
}

void GraphicsCapture::StopSessionLocked() noexcept {
  try {
    if (frame_pool_ != nullptr) frame_pool_.FrameArrived(frame_token_);
    if (item_ != nullptr) item_.Closed(closed_token_);
    if (session_ != nullptr) session_.Close();
    if (frame_pool_ != nullptr) frame_pool_.Close();
  } catch (...) {
  }
  frame_token_ = {};
  closed_token_ = {};
  session_ = nullptr;
  frame_pool_ = nullptr;
  item_ = nullptr;
  active_target_ = {};
}

bool GraphicsCapture::StartLiveOverlay(Error& error) {
  HWND target = nullptr;
  struct SearchContext {
    GraphicsCapture* capture;
    const std::string* preferred;
    HWND* result;
  } search{this, &config_.live_overlay_window_title, &target};
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto* context = reinterpret_cast<SearchContext*>(parameter);
        if (!context->capture->IsWindowCandidate(window)) return TRUE;
        const auto label = GraphicsCapture::WindowLabel(window);
        if (!MatchesPreferredWindow(label, *context->preferred)) return TRUE;
        *context->result = window;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&search));
  if (target == nullptr) {
    error = Error{ErrorComponent::kCapture, "find live overlay window",
                  "open the selected Camera or handcam preview window before starting Klip",
                  {}, {}, config_.live_overlay_window_title};
    return false;
  }

  try {
    std::scoped_lock lock(overlay_session_mutex_);
    overlay_window_ = target;
    overlay_item_ = CreateForWindow(target);
    const auto size = overlay_item_.Size();
    if (size.Width <= 0 || size.Height <= 0) {
      error = Error{ErrorComponent::kCapture, "inspect live overlay window",
                    "the selected window has an invalid size"};
      return false;
    }
    {
      std::scoped_lock preview_lock(preview_mutex_);
      live_overlay_width_ = static_cast<std::uint32_t>(size.Width);
      live_overlay_height_ = static_cast<std::uint32_t>(size.Height);
    }
    overlay_frame_pool_ =
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            interop_device_,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
            size);
    overlay_frame_token_ =
        overlay_frame_pool_.FrameArrived({this, &GraphicsCapture::OnOverlayFrameArrived});
    overlay_closed_token_ =
        overlay_item_.Closed({this, &GraphicsCapture::OnOverlayTargetClosed});
    overlay_session_ = overlay_frame_pool_.CreateCaptureSession(overlay_item_);
    try {
      overlay_session_.IsCursorCaptureEnabled(false);
      overlay_session_.IsBorderRequired(false);
    } catch (const winrt::hresult_error&) {
      logger_.Warning("Handcam cursor or highlight control is unavailable on this Windows build");
    }
    overlay_session_.StartCapture();
    logger_.Info("Live overlay target: " + WindowLabel(target));
    return true;
  } catch (const winrt::hresult_error& exception) {
    error = MakeHresultError(ErrorComponent::kCapture, "start live overlay capture",
                             exception.code().value, config_.live_overlay_window_title);
    StopLiveOverlay();
    return false;
  } catch (const std::exception& exception) {
    error = Error{ErrorComponent::kCapture, "start live overlay capture", exception.what(),
                  {}, {}, config_.live_overlay_window_title};
    StopLiveOverlay();
    return false;
  }
}

void GraphicsCapture::StopLiveOverlay() noexcept {
  std::scoped_lock lock(overlay_session_mutex_);
  try {
    if (overlay_frame_pool_ != nullptr)
      overlay_frame_pool_.FrameArrived(overlay_frame_token_);
    if (overlay_item_ != nullptr) overlay_item_.Closed(overlay_closed_token_);
    if (overlay_session_ != nullptr) overlay_session_.Close();
    if (overlay_frame_pool_ != nullptr) overlay_frame_pool_.Close();
  } catch (...) {
  }
  overlay_frame_token_ = {};
  overlay_closed_token_ = {};
  overlay_session_ = nullptr;
  overlay_frame_pool_ = nullptr;
  overlay_item_ = nullptr;
  overlay_window_ = nullptr;
  std::scoped_lock preview_lock(preview_mutex_);
  live_overlay_view_ = nullptr;
  live_overlay_texture_ = nullptr;
  live_overlay_width_ = 0;
  live_overlay_height_ = 0;
  next_live_overlay_update_ = {};
}

void GraphicsCapture::OnOverlayFrameArrived(
    const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
    const winrt::Windows::Foundation::IInspectable&) {
  if (!running_.load(std::memory_order_acquire)) return;
  try {
    std::scoped_lock session_lock(overlay_session_mutex_);
    if (!running_.load(std::memory_order_relaxed) || sender == nullptr) return;
    auto frame = sender.TryGetNextFrame();
    if (frame == nullptr) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < next_live_overlay_update_) {
      frame.Close();
      return;
    }
    next_live_overlay_update_ = now + std::chrono::milliseconds(33);  // Handcams need ~30 FPS.
    const auto size = frame.ContentSize();
    if (size.Width <= 0 || size.Height <= 0) {
      frame.Close();
      return;
    }
    const auto width = static_cast<std::uint32_t>(size.Width);
    const auto height = static_cast<std::uint32_t>(size.Height);
    {
      std::scoped_lock preview_lock(preview_mutex_);
      if (width != live_overlay_width_ || height != live_overlay_height_) {
        live_overlay_width_ = width;
        live_overlay_height_ = height;
        live_overlay_view_ = nullptr;
        live_overlay_texture_ = nullptr;
        frame.Close();
        sender.Recreate(
            interop_device_,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
            size);
        logger_.Info("Live overlay resolution changed");
        return;
      }
    }
    auto source = TextureFromSurface(frame.Surface());
    if (!source) {
      frame.Close();
      return;
    }

    std::scoped_lock converter_lock(converter_mutex_);
    std::scoped_lock preview_lock(preview_mutex_);
    if (!live_overlay_texture_) {
      D3D11_TEXTURE2D_DESC description{};
      source->GetDesc(&description);
      description.Width = width;
      description.Height = height;
      description.MipLevels = 1;
      description.ArraySize = 1;
      description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      description.SampleDesc.Count = 1;
      description.Usage = D3D11_USAGE_DEFAULT;
      description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
      description.CPUAccessFlags = 0;
      description.MiscFlags = 0;
      auto result = device_->CreateTexture2D(&description, nullptr, live_overlay_texture_.put());
      if (SUCCEEDED(result))
        result = device_->CreateShaderResourceView(live_overlay_texture_.get(), nullptr,
                                                    live_overlay_view_.put());
      if (FAILED(result)) {
        live_overlay_texture_ = nullptr;
        live_overlay_view_ = nullptr;
        frame.Close();
        logger_.Warning(MakeHresultError(ErrorComponent::kCapture,
                                         "allocate live overlay texture", result)
                            .ToString());
        return;
      }
    }
    context_->CopyResource(live_overlay_texture_.get(), source.get());
    frame.Close();
  } catch (const std::exception& exception) {
    logger_.Warning(Error{ErrorComponent::kCapture, "receive live overlay frame",
                          exception.what()}
                        .ToString());
  }
}

void GraphicsCapture::OnOverlayTargetClosed(
    const winrt::Windows::Graphics::Capture::GraphicsCaptureItem&,
    const winrt::Windows::Foundation::IInspectable&) {
  std::scoped_lock lock(preview_mutex_);
  live_overlay_view_ = nullptr;
  live_overlay_texture_ = nullptr;
  logger_.Warning("Live overlay window closed; main capture continues without it");
}

void GraphicsCapture::OnFrameArrived(
    const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
    const winrt::Windows::Foundation::IInspectable&) {
  if (!running_.load(std::memory_order_acquire)) return;
  try {
    std::scoped_lock lock(session_mutex_);
    if (!running_.load(std::memory_order_relaxed) || sender == nullptr) return;
    auto frame = sender.TryGetNextFrame();
    if (frame == nullptr) return;
    const auto size = frame.ContentSize();
    if (size.Width <= 0 || size.Height <= 0) return;
    const auto width = static_cast<std::uint32_t>(size.Width);
    const auto height = static_cast<std::uint32_t>(size.Height);
    if (width != current_width_ || height != current_height_) {
      current_width_ = width;
      current_height_ = height;
      frame.Close();
      sender.Recreate(interop_device_,
                      winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                      3, size);
      logger_.Info("Capture resolution changed");
      return;
    }
    const auto pts = Timestamp100ns();
    const auto minimum_interval =
        std::max<std::int64_t>(1, 10'000'000LL / static_cast<std::int64_t>(config_.target_fps));
    auto previous = last_enqueued_pts_.load(std::memory_order_relaxed);
    if (previous >= 0 && pts - previous < minimum_interval) {
      frame.Close();
      return;
    }
    last_enqueued_pts_.store(pts, std::memory_order_relaxed);
    auto texture = TextureFromSurface(frame.Surface());
    if (!texture) return;
    captured_frames_.fetch_add(1, std::memory_order_relaxed);
    raw_queue_->TryPush(
        RawFrame{std::move(frame), std::move(texture), width, height, pts,
                 capture_generation_.load(std::memory_order_acquire)});
  } catch (const std::exception& exception) {
    const Error error{ErrorComponent::kCapture, "receive frame", exception.what()};
    state_.SetError(error);
    logger_.ErrorMessage(error);
  }
}

void GraphicsCapture::OnTargetClosed(const winrt::Windows::Graphics::Capture::GraphicsCaptureItem&,
                                     const winrt::Windows::Foundation::IInspectable&) {
  target_dirty_.store(true, std::memory_order_release);
  logger_.Warning("Capture target closed; selecting a fallback target");
}

bool GraphicsCapture::LoadStaticOverlay(Error& error) {
  if (config_.static_overlay_path.empty()) {
    error = Error{ErrorComponent::kCapture, "load static overlay",
                  "choose a PNG, JPG, or BMP image first"};
    return false;
  }
  auto path = config_.static_overlay_path;
  if (path.is_relative()) path = std::filesystem::current_path() / path;

  winrt::com_ptr<IWICImagingFactory> factory;
  auto result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(factory.put()));
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "create image decoder", result,
                             path.string());
    return false;
  }
  winrt::com_ptr<IWICBitmapDecoder> decoder;
  result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                               WICDecodeMetadataCacheOnLoad, decoder.put());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "open static overlay image", result,
                             path.string());
    return false;
  }
  winrt::com_ptr<IWICBitmapFrameDecode> frame;
  result = decoder->GetFrame(0, frame.put());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "decode static overlay image", result,
                             path.string());
    return false;
  }
  UINT width = 0;
  UINT height = 0;
  result = frame->GetSize(&width, &height);
  if (FAILED(result) || width == 0 || height == 0 || width > 8192 || height > 8192) {
    error = Error{ErrorComponent::kCapture, "inspect static overlay image",
                  "image dimensions must be between 1 and 8192 pixels", {}, {}, path.string()};
    return false;
  }
  winrt::com_ptr<IWICFormatConverter> converter;
  result = factory->CreateFormatConverter(converter.put());
  if (SUCCEEDED(result))
    result = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "convert static overlay image", result,
                             path.string());
    return false;
  }
  const auto stride = width * 4U;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height);
  result = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "read static overlay pixels", result,
                             path.string());
    return false;
  }
  D3D11_TEXTURE2D_DESC description{};
  description.Width = width;
  description.Height = height;
  description.MipLevels = 1;
  description.ArraySize = 1;
  description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  description.SampleDesc.Count = 1;
  // Video-processor input views require a DEFAULT texture and either no bind flags or a
  // video-compatible bind combination. RENDER_TARGET keeps the texture eligible for the
  // compositor while SHADER_RESOURCE lets the dashboard preview display the same allocation.
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = pixels.data();
  data.SysMemPitch = stride;
  winrt::com_ptr<ID3D11Texture2D> texture;
  result = device_->CreateTexture2D(&description, &data, texture.put());
  winrt::com_ptr<ID3D11ShaderResourceView> view;
  if (SUCCEEDED(result))
    result = device_->CreateShaderResourceView(texture.get(), nullptr, view.put());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "upload static overlay image", result,
                             path.string());
    return false;
  }
  {
    std::scoped_lock lock(preview_mutex_);
    static_overlay_texture_ = std::move(texture);
    static_overlay_view_ = std::move(view);
    static_overlay_width_ = width;
    static_overlay_height_ = height;
  }
  logger_.Info("Static overlay loaded: " + path.string());
  return true;
}

bool GraphicsCapture::EnsureConverter(std::uint32_t width, std::uint32_t height, Error& error) {
  if (processor_ != nullptr && width == processor_width_ && height == processor_height_)
    return true;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC description{};
  description.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  description.InputWidth = width;
  description.InputHeight = height;
  description.OutputWidth = config_.output_width == 0 ? width : config_.output_width;
  description.OutputHeight = config_.output_height == 0 ? height : config_.output_height;
  description.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  processor_enumerator_ = nullptr;
  processor_ = nullptr;
  auto result =
      video_device_->CreateVideoProcessorEnumerator(&description, processor_enumerator_.put());
  if (SUCCEEDED(result)) {
    result = video_device_->CreateVideoProcessor(processor_enumerator_.get(), 0, processor_.put());
  }
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "create BGRA-to-NV12 converter", result);
    return false;
  }
  processor_width_ = width;
  processor_height_ = height;
  {
    std::scoped_lock lock(texture_pool_mutex_);
    texture_pool_.clear();
    texture_pool_width_ = description.OutputWidth;
    texture_pool_height_ = description.OutputHeight;
  }
  return true;
}

bool GraphicsCapture::Convert(const RawFrame& input, ConvertedFrame& output, Error& error) {
  std::scoped_lock converter_lock(converter_mutex_);
  if (!EnsureConverter(input.width, input.height, error)) return false;
  const auto output_width = config_.output_width == 0 ? input.width : config_.output_width;
  const auto output_height = config_.output_height == 0 ? input.height : config_.output_height;
  auto texture = AcquireNv12Texture(output_width, output_height);
  if (!texture) {
    error = Error{ErrorComponent::kCapture, "allocate NV12 texture",
                  "ID3D11Device::CreateTexture2D failed"};
    return false;
  }
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
  input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
  output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  winrt::com_ptr<ID3D11VideoProcessorInputView> input_view;
  winrt::com_ptr<ID3D11VideoProcessorOutputView> output_view;
  auto result = video_device_->CreateVideoProcessorInputView(
      input.texture.get(), processor_enumerator_.get(), &input_desc, input_view.put());
  if (SUCCEEDED(result)) {
    result = video_device_->CreateVideoProcessorOutputView(
        texture.get(), processor_enumerator_.get(), &output_desc, output_view.put());
  }
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "create video processor view", result);
    return false;
  }
  winrt::com_ptr<ID3D11VideoProcessorInputView> overlay_input_view;
  winrt::com_ptr<ID3D11Texture2D> overlay_texture;
  std::uint32_t overlay_width = 0;
  std::uint32_t overlay_height = 0;
  {
    std::scoped_lock preview_lock(preview_mutex_);
    if (config_.static_overlay_enabled && static_overlay_texture_) {
      overlay_texture = static_overlay_texture_;
      overlay_width = static_overlay_width_;
      overlay_height = static_overlay_height_;
    } else if (config_.live_overlay_enabled && live_overlay_texture_) {
      overlay_texture = live_overlay_texture_;
      overlay_width = live_overlay_width_;
      overlay_height = live_overlay_height_;
    }
  }
  bool overlay_ready = false;
  if (overlay_texture) {
    D3D11_VIDEO_PROCESSOR_CAPS capabilities{};
    if (SUCCEEDED(processor_enumerator_->GetVideoProcessorCaps(&capabilities)) &&
        capabilities.MaxInputStreams >= 2) {
      result = video_device_->CreateVideoProcessorInputView(
          overlay_texture.get(), processor_enumerator_.get(), &input_desc, overlay_input_view.put());
      overlay_ready = SUCCEEDED(result);
    }
    if (!overlay_ready && !overlay_warning_logged_) {
      logger_.Warning("The current GPU video processor cannot composite the static overlay; "
                      "capture continues without it");
      overlay_warning_logged_ = true;
    }
  }
  RECT source{0, 0, static_cast<LONG>(input.width), static_cast<LONG>(input.height)};
  RECT output_rect{0, 0, static_cast<LONG>(output_width), static_cast<LONG>(output_height)};
  RECT destination = output_rect;
  if (config_.scaling_mode == VideoScalingMode::kFit && input.width > 0 && input.height > 0) {
    const auto input_aspect_scaled =
        static_cast<std::uint64_t>(input.width) * output_height;
    const auto output_aspect_scaled =
        static_cast<std::uint64_t>(output_width) * input.height;
    if (input_aspect_scaled > output_aspect_scaled) {
      const auto fitted_height = static_cast<LONG>(
          static_cast<std::uint64_t>(output_width) * input.height / input.width);
      const auto offset = (static_cast<LONG>(output_height) - fitted_height) / 2;
      destination.top = offset;
      destination.bottom = offset + fitted_height;
    } else if (input_aspect_scaled < output_aspect_scaled) {
      const auto fitted_width = static_cast<LONG>(
          static_cast<std::uint64_t>(output_height) * input.width / input.height);
      const auto offset = (static_cast<LONG>(output_width) - fitted_width) / 2;
      destination.left = offset;
      destination.right = offset + fitted_width;
    }
  }
  D3D11_VIDEO_COLOR background{};
  background.RGBA.A = 1.0F;
  video_context_->VideoProcessorSetOutputBackgroundColor(processor_.get(), FALSE, &background);
  // Windows Graphics Capture supplies full-range RGB. Declare both sides of the conversion
  // explicitly so the driver produces the same BT.709 limited-range NV12 convention that OBS
  // uses for SDR recording instead of choosing a vendor-dependent default matrix or range.
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE input_color{};
  input_color.RGB_Range = 0;       // Full-range RGB.
  input_color.YCbCr_Matrix = 1;    // BT.709 when a matrix is consulted.
  input_color.Nominal_Range = 2;   // 0-255.
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_color{};
  output_color.RGB_Range = 1;
  output_color.YCbCr_Matrix = 1;   // BT.709.
  output_color.Nominal_Range = 1;  // 16-235 (limited/video range).
  video_context_->VideoProcessorSetStreamFrameFormat(
      processor_.get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  video_context_->VideoProcessorSetStreamColorSpace(processor_.get(), 0, &input_color);
  video_context_->VideoProcessorSetOutputColorSpace(processor_.get(), &output_color);
  video_context_->VideoProcessorSetOutputTargetRect(processor_.get(), TRUE, &output_rect);
  video_context_->VideoProcessorSetStreamSourceRect(processor_.get(), 0, TRUE, &source);
  video_context_->VideoProcessorSetStreamDestRect(processor_.get(), 0, TRUE, &destination);
  D3D11_VIDEO_PROCESSOR_STREAM streams[2]{};
  streams[0].Enable = TRUE;
  streams[0].pInputSurface = input_view.get();
  UINT stream_count = 1;
  if (overlay_ready) {
    RECT overlay_source{0, 0, static_cast<LONG>(overlay_width),
                        static_cast<LONG>(overlay_height)};
    const auto output_w = static_cast<double>(output_width);
    const auto output_h = static_cast<double>(output_height);
    RECT overlay_destination{
        static_cast<LONG>(std::lround(config_.static_overlay_x * output_w)),
        static_cast<LONG>(std::lround(config_.static_overlay_y * output_h)),
        static_cast<LONG>(std::lround((config_.static_overlay_x +
                                      config_.static_overlay_width) * output_w)),
        static_cast<LONG>(std::lround((config_.static_overlay_y +
                                      config_.static_overlay_height) * output_h))};
    overlay_destination.right =
        std::max(overlay_destination.left + 1, overlay_destination.right);
    overlay_destination.bottom =
        std::max(overlay_destination.top + 1, overlay_destination.bottom);
    video_context_->VideoProcessorSetStreamSourceRect(processor_.get(), 1, TRUE,
                                                       &overlay_source);
    video_context_->VideoProcessorSetStreamDestRect(processor_.get(), 1, TRUE,
                                                     &overlay_destination);
    video_context_->VideoProcessorSetStreamAlpha(
        processor_.get(), 1, TRUE, static_cast<float>(config_.static_overlay_opacity));
    video_context_->VideoProcessorSetStreamFrameFormat(
        processor_.get(), 1, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    video_context_->VideoProcessorSetStreamColorSpace(processor_.get(), 1, &input_color);
    streams[1].Enable = TRUE;
    streams[1].pInputSurface = overlay_input_view.get();
    stream_count = 2;
  }
  result = video_context_->VideoProcessorBlt(processor_.get(), output_view.get(), 0,
                                              stream_count, streams);
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "convert frame to NV12", result);
    return false;
  }
  const auto fence_value = fence_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
  result = context4_->Signal(fence_.get(), fence_value);
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kCapture, "signal conversion fence", result);
    return false;
  }
  output = ConvertedFrame{std::move(texture), output_width, output_height, input.pts_100ns,
                          fence_value, input.generation};
  return true;
}

void GraphicsCapture::UpdatePreview(const RawFrame& input) noexcept {
  if (!preview_enabled_.load(std::memory_order_acquire) || !input.texture) return;
  const auto now = std::chrono::steady_clock::now();
  if (now < next_preview_update_) return;
  next_preview_update_ = now + std::chrono::milliseconds(66);  // About 15 FPS.

  std::scoped_lock lock(preview_mutex_);
  if (!preview_texture_ || preview_width_ != input.width || preview_height_ != input.height) {
    D3D11_TEXTURE2D_DESC description{};
    input.texture->GetDesc(&description);
    description.Width = input.width;
    description.Height = input.height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = 0;
    description.MiscFlags = 0;
    winrt::com_ptr<ID3D11Texture2D> texture;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, texture.put()))) return;
    winrt::com_ptr<ID3D11ShaderResourceView> view;
    if (FAILED(device_->CreateShaderResourceView(texture.get(), nullptr, view.put()))) return;
    preview_texture_ = std::move(texture);
    preview_view_ = std::move(view);
    preview_width_ = input.width;
    preview_height_ = input.height;
  }
  context_->CopyResource(preview_texture_.get(), input.texture.get());
}

winrt::com_ptr<ID3D11Texture2D> GraphicsCapture::CreateNv12Texture(std::uint32_t width,
                                                                   std::uint32_t height) const {
  D3D11_TEXTURE2D_DESC description{};
  description.Width = width;
  description.Height = height;
  description.MipLevels = 1;
  description.ArraySize = 1;
  description.Format = DXGI_FORMAT_NV12;
  description.SampleDesc.Count = 1;
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
#ifdef D3D11_BIND_VIDEO_ENCODER
  description.BindFlags |= D3D11_BIND_VIDEO_ENCODER;
#endif
  winrt::com_ptr<ID3D11Texture2D> texture;
  if (FAILED(device_->CreateTexture2D(&description, nullptr, texture.put()))) {
    return nullptr;
  }
  return texture;
}

winrt::com_ptr<ID3D11Texture2D> GraphicsCapture::AcquireNv12Texture(std::uint32_t width,
                                                                    std::uint32_t height) {
  {
    std::scoped_lock lock(texture_pool_mutex_);
    if (width == texture_pool_width_ && height == texture_pool_height_ && !texture_pool_.empty()) {
      auto texture = std::move(texture_pool_.back());
      texture_pool_.pop_back();
      return texture;
    }
  }
  return CreateNv12Texture(width, height);
}

void GraphicsCapture::RecycleNv12Texture(ID3D11Texture2D* texture, std::uint32_t width,
                                         std::uint32_t height) noexcept {
  if (texture == nullptr) return;
  std::scoped_lock lock(texture_pool_mutex_);
  if (recycle_enabled_.load(std::memory_order_acquire) && width == texture_pool_width_ &&
      height == texture_pool_height_ && texture_pool_.size() < config_.encode_queue_capacity + 4) {
    winrt::com_ptr<ID3D11Texture2D> owned;
    owned.attach(texture);
    texture_pool_.push_back(std::move(owned));
  } else {
    texture->Release();
  }
}

winrt::com_ptr<ID3D11Texture2D> GraphicsCapture::TextureFromSurface(
    const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface& surface) const {
  auto access =
      surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
  winrt::com_ptr<ID3D11Texture2D> texture;
  return SUCCEEDED(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void())) ? texture
                                                                                        : nullptr;
}

void GraphicsCapture::ProcessingLoop(std::stop_token stop_token) noexcept {
  try {
    while (!stop_token.stop_requested() && running_.load(std::memory_order_acquire)) {
      RawFrame raw;
      if (!raw_queue_->WaitPop(raw, stop_token)) break;
      // When conversion falls behind, process the newest captured frame and release
      // stale queued frames. Catching up by converting old frames only adds GPU load
      // and makes the replay lag farther behind the game.
      RawFrame newer;
      while (raw_queue_->TryPop(newer)) {
        if (raw.frame != nullptr) {
          try {
            raw.frame.Close();
          } catch (...) {
          }
        }
        raw = std::move(newer);
        coalesced_raw_frames_.fetch_add(1, std::memory_order_relaxed);
      }
      if (raw.generation != capture_generation_.load(std::memory_order_acquire)) continue;
      UpdatePreview(raw);
      ConvertedFrame converted;
      Error error;
      const bool converted_ok = Convert(raw, converted, error);
      if (raw.frame != nullptr) {
        try {
          raw.frame.Close();
        } catch (const winrt::hresult_error& exception) {
          logger_.Warning(MakeHresultError(ErrorComponent::kCapture, "close captured frame",
                                           exception.code().value)
                              .ToString());
        }
      }
      if (!converted_ok) {
        state_.SetError(error);
        logger_.ErrorMessage(error);
        continue;
      }
      encode_queue_->TryPush(std::move(converted));
    }
  } catch (const std::exception& exception) {
    Error error{ErrorComponent::kCapture, "frame conversion worker", exception.what()};
    state_.SetError(error);
    logger_.ErrorMessage(error);
  }
}

void GraphicsCapture::EncodingLoop(std::stop_token stop_token) noexcept {
  try {
    winrt::com_ptr<ID3D11Texture2D> latest;
    std::uint32_t latest_width = 0;
    std::uint32_t latest_height = 0;
    std::uint64_t latest_generation = 0;
    const auto target_fps = std::max<std::uint32_t>(1, config_.target_fps);
    bool timeline_started = false;
    bool timeline_has_encoded_frame = false;
    auto timeline_origin = std::chrono::steady_clock::time_point{};
    std::int64_t media_origin_100ns = 0;
    std::uint64_t frame_number = 0;
    auto next_encode_error_report = std::chrono::steady_clock::time_point{};
    const auto recycle_latest = [&] {
      if (latest)
        RecycleNv12Texture(latest.detach(), latest_width, latest_height);
      latest_width = 0;
      latest_height = 0;
      latest_generation = 0;
    };
    const auto wait_for_fence = [&](std::uint64_t value) {
      const auto result = fence_->SetEventOnCompletion(value, fence_event_.Get());
      if (FAILED(result) || WaitForSingleObject(fence_event_.Get(), 1000) != WAIT_OBJECT_0) {
        Error error{ErrorComponent::kCapture, "wait for GPU frame",
                    "GPU fence wait failed or timed out"};
        state_.SetError(error);
        logger_.ErrorMessage(error);
        return false;
      }
      return true;
    };

    while (!stop_token.stop_requested() && running_.load(std::memory_order_acquire)) {
      const auto generation = capture_generation_.load(std::memory_order_acquire);
      if (latest && latest_generation != generation) {
        recycle_latest();
        timeline_started = false;
        timeline_has_encoded_frame = false;
      }

      ConvertedFrame candidate;
      while (encode_queue_->TryPop(candidate)) {
        if (candidate.generation != generation || !wait_for_fence(candidate.fence_value))
          continue;
        const bool start_timeline = !latest || latest_generation != candidate.generation;
        recycle_latest();
        latest = std::move(candidate.texture);
        latest_width = candidate.width;
        latest_height = candidate.height;
        latest_generation = candidate.generation;
        if (start_timeline) {
          // Opening a hardware encoder with lookahead can take several frame intervals. Start the
          // media clock only after that one-time work completes so an immediate replay does not
          // contain artificial cadence holes at its opening keyframe.
          timeline_started = false;
          timeline_has_encoded_frame = false;
        }
      }

      if (!latest) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }

      if (!timeline_started) {
        const auto now = std::chrono::steady_clock::now();
        Error error;
        if (!encoder_.Prepare(latest_width, latest_height, error)) {
          if (now >= next_encode_error_report) {
            state_.SetError(error);
            logger_.ErrorMessage(error);
            next_encode_error_report = now + std::chrono::seconds(5);
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        timeline_origin = std::chrono::steady_clock::now();
        media_origin_100ns = Timestamp100ns();
        frame_number = 0;
        timeline_started = true;
        timeline_has_encoded_frame = false;
      }

      auto now = std::chrono::steady_clock::now();
      const auto elapsed_ns =
          std::chrono::duration_cast<std::chrono::nanoseconds>(now - timeline_origin).count();
      if (elapsed_ns > 0) {
        // If the encoder genuinely stalls, skip expired schedule slots instead of submitting a
        // burst of stale frames. Under normal load frame_number advances one at a time, including
        // deliberate repeats when WGC supplies 59.94-ish input for a requested 60 FPS output.
        const auto due_frame = static_cast<std::uint64_t>(elapsed_ns) * target_fps /
                               1'000'000'000ULL;
        if (due_frame > frame_number) frame_number = due_frame;
      }
      const auto next_frame =
          timeline_origin + std::chrono::nanoseconds(
                                FixedVideoTimestamp100ns(0, frame_number, target_fps) * 100);
      if (now < next_frame) {
        std::this_thread::sleep_until(next_frame);
        continue;
      }

      auto frame = AcquireNv12Texture(latest_width, latest_height);
      if (!frame) {
        Error error{ErrorComponent::kCapture, "allocate repeated NV12 frame",
                    "ID3D11Device::CreateTexture2D failed"};
        state_.SetError(error);
        logger_.ErrorMessage(error);
        ++frame_number;
        continue;
      }
      // The conversion worker also signals this fence. Reserve the value while holding the
      // same lock that covers its Signal call so values cannot reach the GPU out of order.
      // ID3D11Fence rejects a signal that moves its timeline backwards with E_INVALIDARG.
      std::uint64_t fence_value = 0;
      HRESULT signal_result = S_OK;
      {
        std::scoped_lock converter_lock(converter_mutex_);
        context_->CopyResource(frame.get(), latest.get());
        fence_value = fence_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
        signal_result = context4_->Signal(fence_.get(), fence_value);
      }
      if (FAILED(signal_result) || !wait_for_fence(fence_value)) {
        ++frame_number;
        continue;
      }
      if (latest_generation != capture_generation_.load(std::memory_order_acquire)) {
        ++frame_number;
        continue;
      }

      const auto started = std::chrono::steady_clock::now();
      auto recycler = [this](ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height) {
        RecycleNv12Texture(texture, width, height);
      };
      Error error;
      const auto presentation_time =
          FixedVideoTimestamp100ns(media_origin_100ns, frame_number, target_fps);
      if (!encoder_.Encode(frame.get(), latest_width, latest_height, presentation_time,
                           std::move(recycler), error)) {
        if (now >= next_encode_error_report) {
          state_.SetError(error);
          logger_.ErrorMessage(error);
          next_encode_error_report = now + std::chrono::seconds(5);
        }
        if (!timeline_has_encoded_frame) timeline_started = false;
      } else {
        encoded_frames_.fetch_add(1, std::memory_order_relaxed);
        next_encode_error_report = {};
        timeline_has_encoded_frame = true;
      }
      const auto latency =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
              .count();
      PublishMetrics(latency);
      if (timeline_started) ++frame_number;
    }
    recycle_latest();
  } catch (const std::exception& exception) {
    Error error{ErrorComponent::kVideoEncoder, "video encoding worker", exception.what()};
    state_.SetError(error);
    logger_.ErrorMessage(error);
  }
}

void GraphicsCapture::TargetLoop(std::stop_token stop_token) noexcept {
  try {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    while (!stop_token.stop_requested() && running_.load(std::memory_order_acquire)) {
      if (std::chrono::steady_clock::now() - last_source_refresh_ >= std::chrono::seconds(5))
        RefreshSourceOptions();
      const auto target = DetermineTarget();
      const bool dirty = target_dirty_.exchange(false, std::memory_order_acq_rel);
      if (target.kind == TargetKind::kNone) {
        if (dirty || active_target_.kind != TargetKind::kNone) {
          std::scoped_lock lock(session_mutex_);
          StopSessionLocked();
          capture_generation_.fetch_add(1, std::memory_order_acq_rel);
          encoder_.RestartTimeline();
          state_.SetTarget(CaptureTargetMode::kGameWindow, "Choose a game or window", 0);
          state_.SetStatus(CaptureStatus::kIdle, "Choose a capture source");
        }
      } else if (dirty || !(target == active_target_)) {
        std::scoped_lock lock(session_mutex_);
        StopSessionLocked();
        capture_generation_.fetch_add(1, std::memory_order_acq_rel);
        encoder_.RestartTimeline();
        Error error;
        if (!StartSessionLocked(target, error)) {
          state_.SetError(error);
          logger_.ErrorMessage(error);
          if (target.kind == TargetKind::kWindow) {
            selected_window_.store(0, std::memory_order_release);
            state_.SetTarget(CaptureTargetMode::kGameWindow, "Choose a game or window", 0);
            state_.SetStatus(CaptureStatus::kIdle, "Choose a capture source");
          }
        }
      }
      for (int i = 0; i < 25 && !stop_token.stop_requested(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
  } catch (const std::exception& exception) {
    Error error{ErrorComponent::kCapture, "capture-target worker", exception.what()};
    state_.SetError(error);
    logger_.ErrorMessage(error);
  }
}

std::int64_t GraphicsCapture::Timestamp100ns() const noexcept {
  LARGE_INTEGER current{};
  QueryPerformanceCounter(&current);
  return ((current.QuadPart - qpc_origin_) * 10'000'000LL) / qpc_frequency_;
}

void GraphicsCapture::PublishMetrics(double encode_latency_ms) {
  static thread_local auto last_time = std::chrono::steady_clock::now();
  static thread_local std::uint64_t last_frames = 0;
  static thread_local double latency_total_ms = 0.0;
  static thread_local std::uint64_t latency_samples = 0;
  latency_total_ms += encode_latency_ms;
  ++latency_samples;
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration<double>(now - last_time).count();
  if (elapsed < 0.5) return;
  const auto frames = encoded_frames_.load(std::memory_order_relaxed);
  const auto fps = static_cast<double>(frames - last_frames) / elapsed;
  const auto average_latency = latency_samples == 0
                                   ? 0.0
                                   : latency_total_ms / static_cast<double>(latency_samples);
  last_time = now;
  last_frames = frames;
  latency_total_ms = 0.0;
  latency_samples = 0;
  state_.SetCaptureMetrics(fps, captured_frames_.load(std::memory_order_relaxed),
                           raw_queue_->Dropped() +
                               coalesced_raw_frames_.load(std::memory_order_relaxed),
                           encode_queue_->Dropped(),
                           raw_queue_->Size(), encode_queue_->Size(), average_latency);
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem GraphicsCapture::CreateForWindow(
    HWND window) {
  auto interop =
      winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                    IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  winrt::check_hresult(interop->CreateForWindow(
      window, __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureItem),
      winrt::put_abi(item)));
  return item;
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem GraphicsCapture::CreateForMonitor(
    HMONITOR monitor) {
  auto interop =
      winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                    IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  winrt::check_hresult(interop->CreateForMonitor(
      monitor, __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureItem),
      winrt::put_abi(item)));
  return item;
}

}  // namespace klip
