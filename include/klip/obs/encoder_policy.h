#pragma once

#include "klip/core/config.h"

namespace klip {
struct ObsNvencQualityPolicy {
  const char* preset;
  const char* multipass;
  bool adaptive_quantization;
};

// Performance is an explicit efficiency/quality tradeoff. Keep Balanced's
// OBS-quality configuration unchanged; never apply this based on GPU load alone.
constexpr ObsNvencQualityPolicy ObsNvencPolicyFor(EncoderQuality quality) noexcept {
  switch (quality) {
    case EncoderQuality::kPerformance: return {"p3", "disabled", false};
    case EncoderQuality::kQuality: return {"p7", "fullres", true};
    case EncoderQuality::kBalanced: return {"p5", "qres", true};
  }
  return {"p5", "qres", true};
}
}  // namespace klip
