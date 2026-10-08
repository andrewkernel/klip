#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>

#include "klip/core/error.h"
#include "klip/core/logger.h"
#include "klip/media/packet.h"
#include "klip/media/packet_router.h"

namespace klip {

class AudioEncoder {
 public:
  static constexpr int kSampleRate = 48'000;
  static constexpr int kChannels = 2;

  AudioEncoder(PacketRouter& router, Logger& logger);
  ~AudioEncoder() noexcept;

  AudioEncoder(const AudioEncoder&) = delete;
  AudioEncoder& operator=(const AudioEncoder&) = delete;

  bool Initialize(std::int64_t bitrate, Error& error);
  void Shutdown() noexcept;
  void Flush() noexcept;
  bool Encode(const std::vector<float>& interleaved, int frames, std::int64_t pts_100ns,
              Error& error);
  [[nodiscard]] int FrameSize() const;
  [[nodiscard]] bool SnapshotCodec(CodecSnapshot& snapshot) const;
  [[nodiscard]] std::uint64_t SubmittedFrames() const noexcept {
    return submitted_frames_.load(std::memory_order_acquire);
  }

 private:
  void Drain(std::int64_t fallback_pts) noexcept;
  void FlushLocked() noexcept;
  void Normalize(AVPacket* packet, std::int64_t fallback_pts) noexcept;

  PacketRouter& router_;
  Logger& logger_;
  mutable std::mutex mutex_;
  AVCodecContext* codec_ = nullptr;
  std::int64_t last_pts_ = AV_NOPTS_VALUE;
  std::int64_t last_dts_ = AV_NOPTS_VALUE;
  std::atomic<std::uint64_t> submitted_frames_{0};
};

}  // namespace klip
