#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "klip/media/packet.h"

namespace klip {

struct RollingBufferStats {
  std::size_t packets = 0;
  std::size_t bytes = 0;
  double duration_seconds = 0.0;
};

class RollingMediaBuffer {
 public:
  RollingMediaBuffer(double maximum_seconds, std::size_t maximum_bytes);

  RollingMediaBuffer(const RollingMediaBuffer&) = delete;
  RollingMediaBuffer& operator=(const RollingMediaBuffer&) = delete;

  bool Push(const AVPacket* packet, StreamKind kind, AVRational time_base);
  [[nodiscard]] std::vector<EncodedPacket> Snapshot(double seconds) const;
  [[nodiscard]] RollingBufferStats Stats() const;
  void Clear();

 private:
  void EvictLocked();
  void RefreshVideoBoundsLocked() noexcept;

  const double maximum_seconds_;
  const std::size_t maximum_bytes_;
  mutable std::mutex mutex_;
  std::deque<EncodedPacket> packets_;
  std::size_t bytes_ = 0;
  std::int64_t first_video_pts_100ns_ = 0;
  std::int64_t last_video_pts_100ns_ = 0;
  bool has_video_ = false;
};

}  // namespace klip
