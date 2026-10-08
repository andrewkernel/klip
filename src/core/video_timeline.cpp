#include "klip/core/video_timeline.h"

#include <limits>

namespace klip {

std::int64_t FixedVideoTimestamp100ns(std::int64_t origin_100ns, std::uint64_t frame_number,
                                      std::uint32_t frames_per_second) noexcept {
  if (frames_per_second == 0) return origin_100ns;
  constexpr std::uint64_t kTicksPerSecond = 10'000'000ULL;
  const auto seconds = frame_number / frames_per_second;
  const auto remainder = frame_number % frames_per_second;
  const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if (seconds > maximum / kTicksPerSecond) return std::numeric_limits<std::int64_t>::max();
  const auto offset = seconds * kTicksPerSecond + remainder * kTicksPerSecond / frames_per_second;
  if (origin_100ns >= 0 && offset > maximum - static_cast<std::uint64_t>(origin_100ns))
    return std::numeric_limits<std::int64_t>::max();
  return origin_100ns + static_cast<std::int64_t>(offset);
}

bool CaptureFrameIsDue(std::int64_t timestamp_100ns, std::int64_t previous_100ns,
                       std::uint32_t target_fps) noexcept {
  if (target_fps == 0 || previous_100ns < 0) return true;
  if (timestamp_100ns <= previous_100ns) return false;
  // An exact 1/FPS cutoff drops every second frame when a nominal 60 Hz source
  // alternates slightly early and late arrivals. The encoder's fixed clock still
  // caps output at target_fps; this gate only limits excess conversion work.
  // Half an output frame leaves room for capture and scheduler phase drift.
  const auto minimum_interval = 10'000'000LL / (2 * static_cast<std::int64_t>(target_fps));
  return timestamp_100ns - previous_100ns >= minimum_interval;
}

bool VideoFrameIsDueForOutput(std::int64_t source_100ns, std::int64_t output_100ns,
                              std::uint32_t target_fps) noexcept {
  if (source_100ns <= output_100ns || target_fps == 0) return true;
  return source_100ns - output_100ns <= 5'000'000LL / target_fps;
}

std::int64_t AlignedVideoOrigin100ns(std::int64_t source_100ns, std::int64_t now_100ns,
                                    std::uint32_t target_fps) noexcept {
  if (source_100ns < 0 || source_100ns > now_100ns || target_fps == 0) return now_100ns;
  const auto age = static_cast<std::uint64_t>(now_100ns - source_100ns);
  const auto frames = (age / 10'000'000ULL) * target_fps +
                      (age % 10'000'000ULL) * target_fps / 10'000'000ULL;
  return FixedVideoTimestamp100ns(source_100ns, frames, target_fps);
}

void VideoCadenceTracker::Observe(std::int64_t timestamp) noexcept {
  if (first_ < 0 || timestamp <= first_) {
    Reset();
    first_ = timestamp;
    return;
  }
  if (++intervals_ < 8) return;
  const auto expected = FixedVideoTimestamp100ns(0, intervals_, fps_);
  const auto actual = timestamp - first_;
  matches_ = fps_ > 0 && actual >= expected * 9 / 10 && actual <= expected * 11 / 10;
  first_ = timestamp;
  intervals_ = 0;
}

}  // namespace klip
