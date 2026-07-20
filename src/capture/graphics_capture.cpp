#include "klip/capture/graphics_capture.h"

#include <dwmapi.h>
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
  processing_thread_ = {};
  encoding_thread_ = {};
  recycle_enabled_.store(false, std::memory_order_release);
  if (raw_queue_) raw_queue_->Clear();
  if (encode_queue_) encode_queue_->Clear();
  {
    std::scoped_lock lock(texture_pool_mutex_);
    texture_pool_.clear();
  }
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
      if (option.label == config_.preferred_game_title) return true;
      const auto executable = config_.preferred_game_title.rfind("  [");
      return executable != std::string::npos &&
             option.label.ends_with(config_.preferred_game_title.substr(executable));
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
  RECT source{0, 0, static_cast<LONG>(input.width), static_cast<LONG>(input.height)};
  RECT destination{0, 0, static_cast<LONG>(output_width), static_cast<LONG>(output_height)};
  video_context_->VideoProcessorSetOutputTargetRect(processor_.get(), TRUE, &destination);
  video_context_->VideoProcessorSetStreamSourceRect(processor_.get(), 0, TRUE, &source);
  video_context_->VideoProcessorSetStreamDestRect(processor_.get(), 0, TRUE, &destination);
  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable = TRUE;
  stream.pInputSurface = input_view.get();
  result = video_context_->VideoProcessorBlt(processor_.get(), output_view.get(), 0, 1, &stream);
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
    const auto frame_interval = std::chrono::nanoseconds(
        1'000'000'000LL / static_cast<std::int64_t>(config_.target_fps));
    auto next_frame = std::chrono::steady_clock::now();
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
      if (latest && latest_generation != generation) recycle_latest();

      ConvertedFrame candidate;
      while (encode_queue_->TryPop(candidate)) {
        if (candidate.generation != generation || !wait_for_fence(candidate.fence_value))
          continue;
        recycle_latest();
        latest = std::move(candidate.texture);
        latest_width = candidate.width;
        latest_height = candidate.height;
        latest_generation = candidate.generation;
        next_frame = std::chrono::steady_clock::now();
      }

      if (!latest) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }

      const auto now = std::chrono::steady_clock::now();
      if (now < next_frame) {
        std::this_thread::sleep_until(next_frame);
        continue;
      }

      Error error;
      if (!encoder_.Prepare(latest_width, latest_height, error)) {
        if (now >= next_encode_error_report) {
          state_.SetError(error);
          logger_.ErrorMessage(error);
          next_encode_error_report = now + std::chrono::seconds(5);
        }
        next_frame = now + std::chrono::milliseconds(100);
        continue;
      }

      auto frame = AcquireNv12Texture(latest_width, latest_height);
      if (!frame) {
        Error error{ErrorComponent::kCapture, "allocate repeated NV12 frame",
                    "ID3D11Device::CreateTexture2D failed"};
        state_.SetError(error);
        logger_.ErrorMessage(error);
        next_frame = now + frame_interval;
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
        next_frame = now + frame_interval;
        continue;
      }
      if (latest_generation != capture_generation_.load(std::memory_order_acquire)) {
        next_frame = now + frame_interval;
        continue;
      }

      const auto started = std::chrono::steady_clock::now();
      auto recycler = [this](ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height) {
        RecycleNv12Texture(texture, width, height);
      };
      if (!encoder_.Encode(frame.get(), latest_width, latest_height, Timestamp100ns(),
                           std::move(recycler), error)) {
        if (now >= next_encode_error_report) {
          state_.SetError(error);
          logger_.ErrorMessage(error);
          next_encode_error_report = now + std::chrono::seconds(5);
        }
      } else {
        encoded_frames_.fetch_add(1, std::memory_order_relaxed);
        next_encode_error_report = {};
      }
      const auto latency =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
              .count();
      PublishMetrics(latency);
      next_frame += frame_interval;
      if (next_frame < std::chrono::steady_clock::now() - frame_interval)
        next_frame = std::chrono::steady_clock::now() + frame_interval;
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
