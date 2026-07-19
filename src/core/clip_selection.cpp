#include "klip/core/clip_selection.h"

#include <algorithm>
#include <limits>

namespace klip {

PacketRange SelectClipRange(const std::vector<PacketDescriptor>& packets, double duration_seconds) {
  if (packets.empty() || duration_seconds <= 0.0) {
    return {};
  }

  std::int64_t latest_video_pts = std::numeric_limits<std::int64_t>::min();
  for (const auto& packet : packets) {
    if (packet.kind == StreamKind::kVideo) {
      latest_video_pts = std::max(latest_video_pts, packet.pts_100ns);
    }
  }
  if (latest_video_pts == std::numeric_limits<std::int64_t>::min()) {
    return {};
  }

  const auto window = static_cast<std::int64_t>(duration_seconds * 10'000'000.0);
  const auto threshold = latest_video_pts - window;
  std::size_t candidate = packets.size();
  for (std::size_t index = 0; index < packets.size(); ++index) {
    if (packets[index].kind == StreamKind::kVideo && packets[index].pts_100ns >= threshold) {
      candidate = index;
      break;
    }
  }
  if (candidate == packets.size()) {
    candidate = 0;
  }

  std::size_t start = packets.size();
  for (std::size_t index = candidate + 1; index > 0; --index) {
    const auto prior = index - 1;
    if (packets[prior].kind == StreamKind::kVideo && packets[prior].keyframe) {
      start = prior;
      break;
    }
  }
  if (start == packets.size()) {
    for (std::size_t index = candidate; index < packets.size(); ++index) {
      if (packets[index].kind == StreamKind::kVideo && packets[index].keyframe) {
        start = index;
        break;
      }
    }
  }
  if (start == packets.size()) {
    return {};
  }

  const auto clip_start = packets[start].pts_100ns;
  while (start > 0 && packets[start - 1].kind == StreamKind::kAudio &&
         packets[start - 1].pts_100ns >= clip_start) {
    --start;
  }

  std::int64_t base = std::numeric_limits<std::int64_t>::max();
  for (std::size_t index = start; index < packets.size(); ++index) {
    base = std::min(base, packets[index].dts_100ns);
  }
  if (base == std::numeric_limits<std::int64_t>::max()) {
    base = clip_start;
  }
  return PacketRange{start, packets.size(), base};
}

std::int64_t RebaseTimestamp(std::int64_t timestamp, std::int64_t base_timestamp) noexcept {
  return std::max<std::int64_t>(0, timestamp - base_timestamp);
}

bool ExceedsRollingLimits(std::int64_t first_pts_100ns, std::int64_t last_pts_100ns,
                          std::size_t buffered_bytes, double maximum_seconds,
                          std::size_t maximum_bytes) noexcept {
  const auto duration =
      static_cast<double>(std::max<std::int64_t>(0, last_pts_100ns - first_pts_100ns)) /
      10'000'000.0;
  return buffered_bytes > maximum_bytes || duration > maximum_seconds;
}

}  // namespace klip
