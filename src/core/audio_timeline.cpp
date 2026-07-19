#include "klip/core/audio_timeline.h"

#include <algorithm>
#include <limits>

namespace klip {
namespace {

constexpr std::int64_t kTicksPerSecond = 10'000'000LL;

}  // namespace

std::int64_t AudioFramesTo100ns(std::int64_t frames, int sample_rate) {
  if (frames <= 0 || sample_rate <= 0) return 0;
  return frames * kTicksPerSecond / sample_rate;
}

int AudioFramesFrom100nsCeil(std::int64_t duration_100ns, int sample_rate) {
  if (duration_100ns <= 0 || sample_rate <= 0) return 0;
  const auto seconds = duration_100ns / kTicksPerSecond;
  const auto remainder = duration_100ns % kTicksPerSecond;
  const auto frames = seconds * sample_rate +
                      (remainder * sample_rate + kTicksPerSecond - 1) / kTicksPerSecond;
  return static_cast<int>(
      std::min<std::int64_t>(frames, std::numeric_limits<int>::max()));
}

bool AudioWindowCovered(std::int64_t source_pts_100ns, std::size_t available_frames,
                        std::int64_t output_pts_100ns, int output_frames, int sample_rate) {
  if (output_frames <= 0 || sample_rate <= 0) return false;
  const auto output_end = output_pts_100ns + AudioFramesTo100ns(output_frames, sample_rate);
  if (source_pts_100ns >= output_end) return true;
  const auto bounded_frames = std::min<std::size_t>(
      available_frames, static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()));
  const auto source_end =
      source_pts_100ns +
      AudioFramesTo100ns(static_cast<std::int64_t>(bounded_frames), sample_rate);
  return source_end >= output_end;
}

}  // namespace klip
