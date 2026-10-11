#include "klip/clips/trim_range.h"

#include <algorithm>
#include <cmath>
namespace klip {
double TrimKeyframeStart(const std::vector<double>& frames, double requested) {
  const auto after =
      std::upper_bound(frames.begin(), frames.end(), requested + 0.000001);
  return after == frames.begin() ? 0 : *std::prev(after);
}
double TrimKeyframeEnd(const std::vector<double>& frames, double requested,
                       double duration) {
  const auto after =
      std::lower_bound(frames.begin(), frames.end(), requested - 0.000001);
  return after == frames.end() ? duration : *after;
}
bool ValidTrimRange(double start, double end, double duration) {
  return std::isfinite(start) && std::isfinite(end) &&
         std::isfinite(duration) && duration > 0 && duration <= 602 &&
         start >= 0 && end <= duration + 0.001 && end - start >= 0.1;
}
}  // namespace klip
