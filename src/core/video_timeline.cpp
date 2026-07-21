#include "klip/core/video_timeline.h"

#include <limits>

namespace klip {

std::int64_t FixedVideoTimestamp100ns(std::int64_t origin_100ns,
                                      std::uint64_t frame_number,
                                      std::uint32_t frames_per_second) noexcept {
  if (frames_per_second == 0) return origin_100ns;
  constexpr std::uint64_t kTicksPerSecond = 10'000'000ULL;
  const auto seconds = frame_number / frames_per_second;
  const auto remainder = frame_number % frames_per_second;
  const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if (seconds > maximum / kTicksPerSecond) return std::numeric_limits<std::int64_t>::max();
  const auto offset = seconds * kTicksPerSecond +
                      remainder * kTicksPerSecond / frames_per_second;
  if (origin_100ns >= 0 && offset > maximum - static_cast<std::uint64_t>(origin_100ns))
    return std::numeric_limits<std::int64_t>::max();
  return origin_100ns + static_cast<std::int64_t>(offset);
}

}  // namespace klip
