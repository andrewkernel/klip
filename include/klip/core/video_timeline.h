#pragma once

#include <cstdint>

namespace klip {

// Converts a zero-based frame number to Klip's shared 100 ns media clock without
// accumulating the rounding error of an integer 16.666 ms frame duration.
std::int64_t FixedVideoTimestamp100ns(std::int64_t origin_100ns,
                                      std::uint64_t frame_number,
                                      std::uint32_t frames_per_second) noexcept;

}  // namespace klip
