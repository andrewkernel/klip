#pragma once

#include <Windows.h>
#include <d3d11_4.h>
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

class GraphicsCapture {
 public:
  GraphicsCapture(VideoEncoder& encoder, ApplicationState& state, Logger& logger);
  ~GraphicsCapture() noexcept;

  GraphicsCapture(const GraphicsCapture&) = delete;
  GraphicsCapture& operator=(const GraphicsCapture&) = delete;

  bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND application_window,
                  const AppConfig& config, Error& error);
  bool Start(Error& error);
  void Stop() noexcept;
  void SetTargetMode(CaptureTargetMode mode);
  void SelectTarget(CaptureTargetMode mode, std::uint64_t source_id);
  void SetBorderRequired(bool required);
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
    std::uint64_t generation = 0;
  };

  enum class TargetKind { kNone, kWindow, kMonitor };
  struct CaptureTarget {
    TargetKind kind = TargetKind::kNone;
    HWND window = nullptr;
    HMONITOR monitor = nullptr;
    bool operator==(const CaptureTarget&) const = default;
  };

  bool CreateInteropDevice(Error& error);
  bool CreateFence(Error& error);
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

  bool EnsureConverter(std::uint32_t width, std::uint32_t height, Error& error);
  bool Convert(const RawFrame& input, ConvertedFrame& output, Error& error);
  winrt::com_ptr<ID3D11Texture2D> CreateNv12Texture(std::uint32_t width,
                                                    std::uint32_t height) const;
  winrt::com_ptr<ID3D11Texture2D> AcquireNv12Texture(std::uint32_t width, std::uint32_t height);
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
  std::atomic<bool> recycle_enabled_{false};
  std::atomic<bool> target_dirty_{false};
  std::atomic<CaptureTargetMode> target_mode_{CaptureTargetMode::kGameWindow};
  std::atomic<std::uintptr_t> selected_window_{0};
  std::atomic<std::uintptr_t> selected_monitor_{0};
  std::atomic<std::int64_t> last_enqueued_pts_{-1};
  std::atomic<std::uint64_t> capture_generation_{1};
  std::atomic<std::uint64_t> fence_counter_{0};
  std::atomic<std::uint64_t> captured_frames_{0};
  std::atomic<std::uint64_t> encoded_frames_{0};
  std::atomic<std::uint64_t> coalesced_raw_frames_{0};
  std::int64_t qpc_origin_ = 0;
  std::int64_t qpc_frequency_ = 0;

  winrt::com_ptr<ID3D11Device> device_;
  winrt::com_ptr<ID3D11DeviceContext> context_;
  winrt::com_ptr<ID3D11Device5> device5_;
  winrt::com_ptr<ID3D11DeviceContext4> context4_;
  winrt::com_ptr<ID3D11VideoDevice> video_device_;
  winrt::com_ptr<ID3D11VideoContext> video_context_;
  winrt::com_ptr<ID3D11Fence> fence_;
  ScopedHandle fence_event_;
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

  std::mutex converter_mutex_;
  winrt::com_ptr<ID3D11VideoProcessorEnumerator> processor_enumerator_;
  winrt::com_ptr<ID3D11VideoProcessor> processor_;
  std::uint32_t processor_width_ = 0;
  std::uint32_t processor_height_ = 0;
  std::mutex texture_pool_mutex_;
  std::vector<winrt::com_ptr<ID3D11Texture2D>> texture_pool_;
  std::uint32_t texture_pool_width_ = 0;
  std::uint32_t texture_pool_height_ = 0;

  std::unique_ptr<SpscQueue<RawFrame>> raw_queue_;
  std::unique_ptr<SpscQueue<ConvertedFrame>> encode_queue_;
  std::jthread processing_thread_;
  std::jthread encoding_thread_;
  std::jthread target_thread_;
};

}  // namespace klip
