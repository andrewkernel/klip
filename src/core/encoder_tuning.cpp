#include "klip/core/encoder_tuning.h"

#include <algorithm>

namespace klip {
namespace {

void Add(EncoderTuning& tuning, std::string key, std::string value) {
  tuning.options.emplace_back(std::move(key), std::move(value));
}

}  // namespace

EncoderTuning BuildEncoderTuning(const std::string& encoder_name,
                                 std::uint32_t frames_per_second,
                                 EncoderQuality quality) {
  EncoderTuning tuning;
  const auto fps = std::max<std::uint32_t>(1, frames_per_second);
  tuning.gop_frames = static_cast<int>(fps * 2);
  tuning.max_b_frames = quality == EncoderQuality::kPerformance ? 0 : 2;
  tuning.low_delay = quality == EncoderQuality::kPerformance;

  Add(tuning, "g", std::to_string(tuning.gop_frames));
  Add(tuning, "bf", std::to_string(tuning.max_b_frames));

  if (encoder_name.starts_with("h264_nvenc")) {
    Add(tuning, "profile", "high");
    Add(tuning, "forced-idr", "1");
    Add(tuning, "rc", "cbr");
    if (quality == EncoderQuality::kPerformance) {
      Add(tuning, "preset", "p3");
      Add(tuning, "tune", "ll");
      Add(tuning, "multipass", "disabled");
      Add(tuning, "rc-lookahead", "0");
      Add(tuning, "spatial-aq", "0");
      Add(tuning, "temporal-aq", "0");
      Add(tuning, "delay", "0");
      // External D3D11 textures remain registered with NVENC until the encoder releases them.
      // Four surfaces is too small for a 60 fps capture pipeline under transient GPU load and
      // causes avcodec_send_frame() to fail with ENOMEM, followed by EINVAL/EAGAIN. Sixteen keeps
      // the low-latency preset bounded while leaving enough headroom for capture/encode overlap.
      Add(tuning, "surfaces", "16");
      Add(tuning, "zerolatency", "1");
    } else {
      // Mirrors OBS's recording-oriented defaults: HQ tuning, P5, quarter-resolution
      // multipass, adaptive quantization, an eight-frame lookahead, and two B-frames.
      Add(tuning, "preset", quality == EncoderQuality::kQuality ? "p6" : "p5");
      Add(tuning, "tune", "hq");
      Add(tuning, "multipass", quality == EncoderQuality::kQuality ? "fullres" : "qres");
      Add(tuning, "rc-lookahead", quality == EncoderQuality::kQuality ? "16" : "8");
      Add(tuning, "spatial-aq", "1");
      Add(tuning, "temporal-aq", "1");
      Add(tuning, "aq-strength", "8");
      Add(tuning, "b_adapt", "1");
    }
  } else if (encoder_name.starts_with("h264_amf")) {
    Add(tuning, "profile", "high");
    Add(tuning, "forced_idr", "1");
    if (quality == EncoderQuality::kPerformance) {
      Add(tuning, "usage", "ultralowlatency");
      Add(tuning, "quality", "speed");
      Add(tuning, "rc", "cbr");
      Add(tuning, "async_depth", "2");
      Add(tuning, "latency", "1");
      Add(tuning, "preanalysis", "0");
    } else {
      Add(tuning, "usage", "transcoding");
      Add(tuning, "quality", "quality");
      Add(tuning, "rc", "cbr");
      Add(tuning, "async_depth", quality == EncoderQuality::kQuality ? "8" : "4");
      Add(tuning, "latency", "0");
      Add(tuning, "vbaq", "1");
      Add(tuning, "high_motion_quality_boost_enable", "1");
      Add(tuning, "preanalysis", quality == EncoderQuality::kQuality ? "1" : "0");
    }
  } else if (encoder_name.starts_with("h264_mf")) {
    Add(tuning, "hw_encoding", encoder_name == "h264_mf_software" ? "0" : "1");
    Add(tuning, "rate_control", "cbr");
    Add(tuning, "scenario", quality == EncoderQuality::kPerformance ? "display_remoting"
                                                                     : "archive");
    Add(tuning, "quality", quality == EncoderQuality::kPerformance ? "60"
                            : quality == EncoderQuality::kQuality   ? "90"
                                                                   : "80");
  }
  return tuning;
}

}  // namespace klip
