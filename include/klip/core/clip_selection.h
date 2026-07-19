#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace klip {

enum class StreamKind { kVideo, kAudio };

struct PacketDescriptor {
  StreamKind kind = StreamKind::kVideo;
  std::int64_t pts_100ns = 0;
  std::int64_t dts_100ns = 0;
  std::int64_t duration_100ns = 0;
  std::size_t bytes = 0;
  bool keyframe = false;
};

struct PacketRange {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::int64_t base_timestamp_100ns = 0;

  [[nodiscard]] bool Empty() const { return begin >= end; }
};

PacketRange SelectClipRange(const std::vector<PacketDescriptor>& packets, double duration_seconds);
std::int64_t RebaseTimestamp(std::int64_t timestamp, std::int64_t base_timestamp) noexcept;
bool ExceedsRollingLimits(std::int64_t first_pts_100ns, std::int64_t last_pts_100ns,
                          std::size_t buffered_bytes, double maximum_seconds,
                          std::size_t maximum_bytes) noexcept;

}  // namespace klip
