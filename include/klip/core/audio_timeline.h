#pragma once

#include <cstddef>
#include <cstdint>

namespace klip {

std::int64_t AudioFramesTo100ns(std::int64_t frames, int sample_rate);
int AudioFramesFrom100nsCeil(std::int64_t duration_100ns, int sample_rate);
bool AudioWindowCovered(std::int64_t source_pts_100ns, std::size_t available_frames,
                        std::int64_t output_pts_100ns, int output_frames, int sample_rate);

}  // namespace klip
