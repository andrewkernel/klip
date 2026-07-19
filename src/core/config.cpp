#include "klip/core/config.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace klip {

std::vector<ValidationIssue> ValidateConfig(const AppConfig& config) {
  std::vector<ValidationIssue> issues;
  const auto reject = [&issues](std::string field, std::string message) {
    issues.push_back({std::move(field), std::move(message)});
  };

  if (config.clip_duration_seconds <= 0.0 || config.clip_duration_seconds > 600.0) {
    reject("clip_duration_seconds", "must be in (0, 600]");
  }
  if (config.target_fps != 30 && config.target_fps != 60) {
    reject("target_fps", "must be 30 or 60");
  }
  if ((config.output_width == 0) != (config.output_height == 0)) {
    reject("output_resolution", "width and height must both be zero or both be set");
  }
  if ((config.output_width != 0 && (config.output_width % 2 != 0)) ||
      (config.output_height != 0 && (config.output_height % 2 != 0))) {
    reject("output_resolution", "NV12 width and height must be even");
  }
  if (config.video_bitrate < 100'000) {
    reject("video_bitrate", "must be at least 100000 bits/s");
  }
  if (config.audio_bitrate < 32'000) {
    reject("audio_bitrate", "must be at least 32000 bits/s");
  }
  if (!std::isfinite(config.desktop_audio_gain) || config.desktop_audio_gain < 0.0 ||
      config.desktop_audio_gain > 2.0) {
    reject("desktop_audio_gain", "must be between 0.0 and 2.0");
  }
  if (!std::isfinite(config.microphone_audio_gain) || config.microphone_audio_gain < 0.0 ||
      config.microphone_audio_gain > 2.0) {
    reject("microphone_audio_gain", "must be between 0.0 and 2.0");
  }
  if (config.output_directory.empty()) {
    reject("output_directory", "must not be empty");
  }
  if (config.recording_directory.empty()) {
    reject("recording_directory", "must not be empty");
  }
  if (config.log_path.empty()) {
    reject("log_path", "must not be empty");
  }
  if (config.raw_frame_queue_capacity < 2 || config.encode_queue_capacity < 2) {
    reject("queue_capacity", "frame queues must hold at least two entries");
  }
  if (config.clip_request_queue_capacity == 0) {
    reject("clip_request_queue_capacity", "must be non-zero");
  }
  if (config.recording_packet_queue_capacity < 64) {
    reject("recording_packet_queue_capacity", "must hold at least 64 packets");
  }
  if (config.rolling_buffer_seconds < config.clip_duration_seconds) {
    reject("rolling_buffer_seconds", "must cover the configured clip duration");
  }
  if (config.rolling_buffer_bytes < 1024 * 1024) {
    reject("rolling_buffer_bytes", "must be at least one MiB");
  }
  if (config.encoder_preferences.empty()) {
    reject("encoder_preferences", "must contain at least one encoder");
  }

  std::unordered_set<std::string> encoders;
  for (const auto& encoder : config.encoder_preferences) {
    if (encoder.empty()) {
      reject("encoder_preferences", "encoder names must not be empty");
    } else if (!encoders.insert(encoder).second) {
      reject("encoder_preferences", "encoder names must be unique");
    }
  }

  return issues;
}

}  // namespace klip
