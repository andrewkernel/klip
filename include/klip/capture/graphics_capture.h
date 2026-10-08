#pragma once

#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "klip/core/application_state.h"
#include "klip/core/bounded_queue.h"
#include "klip/core/config.h"
#include "klip/core/error.h"
#include "klip/core/logger.h"
#include "klip/encoding/video_encoder.h"
#include "klip/platform/scoped_handle.h"

namespace klip {

struct CapturePreview {
  winrt::com_ptr<ID3D11ShaderResourceView> texture;
  winrt::com_ptr<ID3D11ShaderResourceView> overlay_texture;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

class GraphicsCapture {
 public:
  GraphicsCapture(VideoEncoder& encoder, ApplicationState& state, Logger& logger);
  ~GraphicsCapture() noexcept;

  GraphicsCapture(const GraphicsCapture&) = delete;
  GraphicsCapture& operator=(const GraphicsCapture&) = delete;

  bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND application_window,
                  const AppConfig& config, Error& error, bool force_event_query_sync = false);
  bool Start(Error& error);
  void Stop() noexcept;
  void SetTargetMode(CaptureTargetMode mode);
  void SelectTarget(CaptureTargetMode mode, std::uint64_t source_id);
  void SetBorderRequired(bool required);
  void SetPreviewEnabled(bool enabled) noexcept;
  [[nodiscard]] CapturePreview Preview() const;
  [[nodiscard]] int64_t QpcOrigin() const noexcept { return qpc_origin_; }
  [[nodiscard]] int64_t QpcFrequency() const noexcept { return qpc_frequency_; }

 private:
  struct RawFrame {
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame{nullptr};
    winrt::com_ptr<ID3D11Texture2D> texture;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int64_t pts_100ns = 0;
    std::uint64_t generation = 0;
  };

  struct ConvertedFrame {
    winrt::com_ptr<ID3D11Texture2D> texture;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int64_t pts_100ns = 0;
    std::uint64_t fence_value = 0;
    winrt::com_ptr<ID3D11Query> event_query;
    std::uint64_t generation = 0;
  };

  struct WgcClockTrace {
    std::int64_t last_source_time_100ns = 0;
    std::int64_t last_callback_time_100ns = -1;
    std::int64_t source_interval_min_100ns = INT64_MAX;
    std::int64_t source_interval_max_100ns = 0;
    std::int64_t callback_interval_min_100ns = INT64_MAX;
    std::int64_t callback_interval_max_100ns = 0;
    std::uint64_t interval_samples = 0;
    std::uint64_t source_time_regressions = 0;
  };

  enum class FrameClockMode { kUndetermined, kWgcSource, kCallbackArrival };

  enum class TargetKind { kNone, kWindow, kMonitor };
  struct CaptureTarget {
    TargetKind kind = TargetKind::kNone;
    HWND window = nullptr;
    HMONITOR monitor = nullptr;
    bool operator==(const CaptureTarget&) const = default;
  };

  bool CreateInteropDevice(Error& error);
  bool CreateFence(Error& error);
  bool SignalGpuWork(ConvertedFrame& frame, Error& error);
  bool WaitForGpuWork(const ConvertedFrame& frame);
  CaptureTarget DetermineTarget();
  bool IsWindowCandidate(HWND window) const;
  void RefreshSourceOptions();
  static std::string WindowLabel(HWND window);
  static std::string MonitorLabel(HMONITOR monitor);
  bool StartSessionLocked(const CaptureTarget& target, Error& error);
  void StopSessionLocked() noexcept;
  void OnFrameArrived(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
                      const winrt::Windows::Foundation::IInspectable&);
  void OnTargetClosed(const winrt::Windows::Graphics::Capture::GraphicsCaptureItem&,
                      const winrt::Windows::Foundation::IInspectable&);
  void TraceWgcClock(std::int64_t source_time_100ns, std::int64_t callback_time_100ns) noexcept;
  void LogTargetAdapter(HMONITOR monitor, bool is_display_capture);

  bool EnsureConverter(std::uint32_t width, std::uint32_t height, Error& error);
  bool LoadStaticOverlay(Error& error);
  bool StartLiveOverlay(Error& error);
  void StopLiveOverlay() noexcept;
  void OnOverlayFrameArrived(
      const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
      const winrt::Windows::Foundation::IInspectable&);
  void OnOverlayTargetClosed(const winrt::Windows::Graphics::Capture::GraphicsCaptureItem&,
                             const winrt::Windows::Foundation::IInspectable&);
  bool Convert(const RawFrame& input, ConvertedFrame& output, Error& error);
  void UpdatePreview(const RawFrame& input) noexcept;
  winrt::com_ptr<ID3D11Texture2D> CreateNv12Texture(std::uint32_t width,
                                                    std::uint32_t height) const;
  winrt::com_ptr<ID3D11Texture2D> AcquireNv12Texture(std::uint32_t width, std::uint32_t height);
  winrt::com_ptr<ID3D11Texture2D> AcquireEncoderTexture(std::uint32_t width, std::uint32_t height);
  void RecycleEncoderTexture(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height,
                             bool reusable = true) noexcept;
  void DiscardEncoderTexture(ID3D11Texture2D* texture, std::uint32_t width,
                             std::uint32_t height) noexcept;
  void RecycleNv12Texture(ID3D11Texture2D* texture, std::uint32_t width,
                          std::uint32_t height) noexcept;
  winrt::com_ptr<ID3D11Texture2D> TextureFromSurface(
      const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface& surface) const;

  void ProcessingLoop(std::stop_token stop_token) noexcept;
  void EncodingLoop(std::stop_token stop_token) noexcept;
  void TargetLoop(std::stop_token stop_token) noexcept;
  std::int64_t Timestamp100ns() const noexcept;
  void PublishMetrics(double encode_latency_ms);
  static winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateForWindow(HWND window);
  static winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateForMonitor(HMONITOR monitor);

  VideoEncoder& encoder_;
  ApplicationState& state_;
  Logger& logger_;
  AppConfig config_;
  HWND application_window_ = nullptr;
  std::atomic<bool> running_{false};
  std::atomic<bool> capture_target_active_{false};
  std::atomic<bool> recycle_enabled_{false};
  std::atomic<bool> target_dirty_{false};
  std::atomic<bool> preview_enabled_{false};
  std::atomic<CaptureTargetMode> target_mode_{CaptureTargetMode::kGameWindow};
  std::atomic<std::uintptr_t> selected_window_{0};
  std::atomic<std::uintptr_t> selected_monitor_{0};
  std::atomic<std::int64_t> last_enqueued_pts_{-1};
  std::atomic<std::uint64_t> capture_generation_{1};
  std::atomic<std::uint64_t> fence_counter_{0};
  std::atomic<std::uint64_t> captured_frames_{0};
  std::atomic<std::uint64_t> source_frames_{0};
  std::uint64_t unique_submissions_ = 0;
  std::uint64_t repeated_submissions_ = 0;
  std::uint64_t skipped_output_slots_ = 0;
  std::uint64_t failed_submissions_ = 0;
  WgcClockTrace wgc_clock_trace_;
  FrameClockMode frame_clock_mode_ = FrameClockMode::kUndetermined;
  std::atomic<std::uint64_t> submitted_frames_{0};
  std::atomic<std::uint64_t> coalesced_raw_frames_{0};
  std::atomic<std::uint64_t> coalesced_encode_frames_{0};
  std::int64_t qpc_origin_ = 0;
  std::int64_t qpc_frequency_ = 0;

  winrt::com_ptr<ID3D11Device> device_;
  winrt::com_ptr<ID3D11DeviceContext> context_;
  winrt::com_ptr<ID3D11Device5> device5_;
  winrt::com_ptr<ID3D11DeviceContext4> context4_;
  winrt::com_ptr<ID3D11VideoDevice> video_device_;
  winrt::com_ptr<ID3D11VideoContext> video_context_;
  LUID capture_adapter_luid_{};
  std::uint32_t capture_adapter_vendor_id_ = 0;
  std::wstring capture_adapter_name_;
  winrt::com_ptr<ID3D11Fence> fence_;
  ScopedHandle fence_event_;
  bool use_gpu_fence_ = false;
  bool force_event_query_sync_ = false;
  bool capture_trace_enabled_ = false;
  bool wgc_clock_warning_logged_ = false;
  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice interop_device_{nullptr};

  std::mutex session_mutex_;
  CaptureTarget active_target_{};
  std::chrono::steady_clock::time_point last_source_refresh_{};
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool frame_pool_{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
  winrt::event_token frame_token_{};
  winrt::event_token closed_token_{};
  std::uint32_t current_width_ = 0;
  std::uint32_t current_height_ = 0;
  std::uint32_t output_width_ = 0;
  std::uint32_t output_height_ = 0;

  std::mutex overlay_session_mutex_;
  HWND overlay_window_ = nullptr;
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool overlay_frame_pool_{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession overlay_session_{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem overlay_item_{nullptr};
  winrt::event_token overlay_frame_token_{};
  winrt::event_token overlay_closed_token_{};

  std::mutex converter_mutex_;
  winrt::com_ptr<ID3D11VideoProcessorEnumerator> processor_enumerator_;
  winrt::com_ptr<ID3D11VideoProcessor> processor_;
  std::uint32_t processor_width_ = 0;
  std::uint32_t processor_height_ = 0;
  std::mutex texture_pool_mutex_;
  std::vector<winrt::com_ptr<ID3D11Texture2D>> texture_pool_;
  std::vector<winrt::com_ptr<ID3D11Texture2D>> encoder_texture_pool_;
  std::size_t encoder_texture_count_ = 0;
  std::uint32_t encoder_pool_width_ = 0;
  std::uint32_t encoder_pool_height_ = 0;
  std::uint32_t texture_pool_width_ = 0;
  std::uint32_t texture_pool_height_ = 0;

  mutable std::mutex preview_mutex_;
  winrt::com_ptr<ID3D11Texture2D> preview_texture_;
  winrt::com_ptr<ID3D11ShaderResourceView> preview_view_;
  std::uint32_t preview_width_ = 0;
  std::uint32_t preview_height_ = 0;
  std::chrono::steady_clock::time_point next_preview_update_{};
  winrt::com_ptr<ID3D11Texture2D> static_overlay_texture_;
  winrt::com_ptr<ID3D11ShaderResourceView> static_overlay_view_;
  std::uint32_t static_overlay_width_ = 0;
  std::uint32_t static_overlay_height_ = 0;
  winrt::com_ptr<ID3D11Texture2D> live_overlay_texture_;
  winrt::com_ptr<ID3D11ShaderResourceView> live_overlay_view_;
  std::uint32_t live_overlay_width_ = 0;
  std::uint32_t live_overlay_height_ = 0;
  std::chrono::steady_clock::time_point next_live_overlay_update_{};
  bool overlay_warning_logged_ = false;

  std::unique_ptr<SpscQueue<RawFrame>> raw_queue_;
  std::unique_ptr<SpscQueue<ConvertedFrame>> encode_queue_;
  std::jthread processing_thread_;
  std::jthread encoding_thread_;
  std::jthread target_thread_;
};

}  // namespace klip
