#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "klip/core/config.h"

namespace klip {

struct EncoderTuning {
  int gop_frames = 0;
  int max_b_frames = 0;
  bool low_delay = false;
  std::vector<std::pair<std::string, std::string>> options;
};

EncoderTuning BuildEncoderTuning(const std::string& encoder_name,
                                 std::uint32_t frames_per_second,
                                 EncoderQuality quality);

}  // namespace klip
