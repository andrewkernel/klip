#pragma once

#include <d3d11.h>
#include <winrt/base.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "klip/core/application_state.h"
#include "klip/core/config.h"
#include "klip/core/error.h"
#include "klip/core/logger.h"
#include "klip/media/packet.h"
#include "klip/media/packet_router.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/hwcontext.h>
}

namespace klip {

class VideoEncoder {
 public:
  using TextureRecycler = std::function<void(ID3D11Texture2D*, std::uint32_t, std::uint32_t)>;

  VideoEncoder(PacketRouter& router, ApplicationState& state, Logger& logger);
  ~VideoEncoder() noexcept;
  VideoEncoder(const VideoEncoder&) = delete;
  VideoEncoder& operator=(const VideoEncoder&) = delete;

  bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                  std::uint32_t adapter_vendor_id, const AppConfig& config, Error& error);
  void Shutdown() noexcept;
  void Flush() noexcept;
  void RestartTimeline() noexcept;
  bool Prepare(std::uint32_t width, std::uint32_t height, Error& error);
  bool Encode(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height,
              std::int64_t pts_100ns, TextureRecycler recycler, Error& error);
  [[nodiscard]] bool SnapshotCodec(CodecSnapshot& snapshot) const;

 private:
  struct RecycleCookie;
  static void ReleaseTexture(void* opaque, std::uint8_t* data) noexcept;
  bool CreateHardwareDevice(Error& error);
  bool CreateFramesContext(std::uint32_t width, std::uint32_t height, Error& error);
  bool EnsureOpen(std::uint32_t width, std::uint32_t height, Error& error);
  bool TryOpen(const std::string& name, std::uint32_t width, std::uint32_t height, Error& error);
  void Drain(std::int64_t fallback_pts);
  void NormalizeTimestamps(AVPacket* packet, std::int64_t fallback_pts);
  void FlushLocked() noexcept;
  void ReleaseCodec() noexcept;
  std::vector<std::string> BuildPreference() const;
  PacketRouter& router_;
  ApplicationState& state_;
  Logger& logger_;
  AppConfig config_;
  std::uint32_t adapter_vendor_id_ = 0;
  winrt::com_ptr<ID3D11Device> device_;
  winrt::com_ptr<ID3D11DeviceContext> context_;
  winrt::com_ptr<ID3D11Texture2D> staging_texture_;
  std::uint32_t staging_width_ = 0;
  std::uint32_t staging_height_ = 0;
  AVBufferRef* hardware_device_ = nullptr;
  AVBufferRef* hardware_frames_ = nullptr;
  AVCodecContext* codec_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::string active_encoder_;
  std::vector<std::string> runtime_rejected_encoders_;
  std::int64_t last_pts_ = AV_NOPTS_VALUE;
  std::int64_t last_dts_ = AV_NOPTS_VALUE;
  bool force_keyframe_ = true;
  bool logged_first_packet_ = false;
  bool logged_first_keyframe_ = false;
  std::chrono::steady_clock::time_point next_open_attempt_{};
  std::uint32_t failed_width_ = 0;
  std::uint32_t failed_height_ = 0;
  Error last_open_error_{};
  mutable std::mutex mutex_;
};

}  // namespace klip
