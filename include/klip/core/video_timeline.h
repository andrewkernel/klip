#pragma once

#include <cstdint>
#include <utility>

namespace klip {

// NV12 requires even plane dimensions. Source-sized capture can encounter odd window sizes,
// so round down by at most one pixel before creating GPU surfaces.
constexpr std::uint32_t EvenNv12Dimension(std::uint32_t dimension) noexcept {
  return dimension < 2 ? 2 : dimension & ~std::uint32_t{1};
}

// Converts a zero-based frame number to Klip's shared 100 ns media clock without
// accumulating the rounding error of an integer 16.666 ms frame duration.
std::int64_t FixedVideoTimestamp100ns(std::int64_t origin_100ns, std::uint64_t frame_number,
                                      std::uint32_t frames_per_second) noexcept;

// Allow normal capture-clock jitter without admitting every frame from a much faster source.
bool CaptureFrameIsDue(std::int64_t timestamp_100ns, std::int64_t previous_100ns,
                       std::uint32_t target_fps) noexcept;

// Keep samples newer than the midpoint for the following output slot.
bool VideoFrameIsDueForOutput(std::int64_t source_100ns, std::int64_t output_100ns,
                              std::uint32_t target_fps) noexcept;
std::int64_t AlignedVideoOrigin100ns(std::int64_t source_100ns, std::int64_t now_100ns,
                                    std::uint32_t target_fps) noexcept;
// Short rate estimate used to preserve order when input and output run at similar rates.
class VideoCadenceTracker {
 public:
  explicit VideoCadenceTracker(std::uint32_t fps) : fps_(fps) {}
  void Observe(std::int64_t timestamp) noexcept;
  [[nodiscard]] bool MatchesOutput() const noexcept { return matches_; }
  void Reset() noexcept { first_ = -1; intervals_ = 0; matches_ = false; }
 private:
  std::uint32_t fps_;
  std::int64_t first_ = -1;
  std::uint32_t intervals_ = 0;
  bool matches_ = false;
};

// Once GPU work is complete, return resources from a stale capture generation rather than
// dropping their pool ownership when a window/display switch races frame submission.
template <typename Recycle>
bool RecycleIfCaptureGenerationStale(std::uint64_t frame_generation,
                                     std::uint64_t active_generation, Recycle&& recycle) {
  if (frame_generation == active_generation) return false;
  std::forward<Recycle>(recycle)();
  return true;
}

}  // namespace klip
