#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "klip/core/config.h"
#include "klip/core/error.h"

namespace klip {

enum class CaptureStatus { kIdle, kStarting, kBuffering, kSaving, kStopping, kFailed };

struct MetricsSnapshot {
  double capture_fps = 0.0;
  double source_fps = 0.0;
  std::uint64_t captured_frames = 0;
  std::uint64_t dropped_raw_frames = 0;
  std::uint64_t dropped_encode_frames = 0;
  std::size_t raw_queue_depth = 0;
  std::size_t video_queue_depth = 0;
  double encode_latency_ms = 0.0;
  double last_clip_save_ms = 0.0;
  double rolling_buffer_seconds = 0.0;
  std::size_t rolling_buffer_bytes = 0;
  std::size_t rolling_buffer_packets = 0;
  std::size_t recording_queue_depth = 0;
  std::uint64_t dropped_recording_packets = 0;
};

struct CaptureSourceOption {
  std::uint64_t id = 0;
  std::string label;
};

struct AudioApplicationOption {
  std::uint32_t process_id = 0;
  std::string name;
};

struct ApplicationSnapshot {
  CaptureStatus status = CaptureStatus::kIdle;
  CaptureTargetMode target_mode = CaptureTargetMode::kGameWindow;
  std::string capture_target;
  std::uint64_t selected_capture_source_id = 0;
  std::vector<CaptureSourceOption> game_sources;
  std::vector<CaptureSourceOption> display_sources;
  std::vector<CaptureSourceOption> overlay_sources;
  std::string graphics_adapter;
  std::string graphics_feature_level;
  std::string capture_adapter;
  std::string capture_adapter_relationship;
  std::uint32_t graphics_adapter_vendor_id = 0;
  std::string selected_encoder;
  std::vector<std::string> available_encoders;
  std::string encoder_status;
  bool desktop_audio_active = false;
  bool microphone_enabled = false;
  bool microphone_active = false;
  float desktop_audio_level = 0.0F;
  float microphone_level = 0.0F;
  std::vector<std::string> microphones;
  std::vector<AudioApplicationOption> audio_applications;
  int selected_microphone_index = -1;
  std::filesystem::path last_saved_clip;
  bool recording = false;
  bool finalizing_recording = false;
  double recording_seconds = 0.0;
  std::filesystem::path recording_path;
  std::filesystem::path last_saved_recording;
  std::string current_operation;
  std::optional<Error> last_error;
  std::uint64_t error_generation = 0;
  bool settings_restart_required = false;
  std::string settings_message;
  MetricsSnapshot metrics;
};

class ApplicationState {
 public:
  [[nodiscard]] ApplicationSnapshot Snapshot() const;
  void Update(const ApplicationSnapshot& snapshot);
  void SetStatus(CaptureStatus status, std::string operation = {});
  void SetError(Error error);
  void ClearError(ErrorComponent component);
  void SetTarget(CaptureTargetMode mode, std::string description, std::uint64_t source_id = 0);
  void SetCaptureSources(std::vector<CaptureSourceOption> game_sources,
                         std::vector<CaptureSourceOption> display_sources,
                         std::uint64_t selected_source_id);
  void SetOverlaySources(std::vector<CaptureSourceOption> sources);
  void SetGraphicsAdapter(std::string adapter, std::uint32_t vendor_id = 0,
                          std::string feature_level = {});
  void SetCaptureAdapter(std::string adapter, std::string relationship);
  void SetEncoder(std::string encoder);
  void SetAvailableEncoders(std::vector<std::string> encoders);
  void SetEncoderStatus(std::string status);
  void SetAudio(bool desktop_active, bool microphone_active, float desktop_level,
                float microphone_level);
  void SetMicrophoneEnabled(bool enabled);
  void SetMicrophones(std::vector<std::string> microphones, int selected_index);
  void SetAudioApplications(std::vector<AudioApplicationOption> applications);
  void SetMetrics(const MetricsSnapshot& metrics);
  void SetCaptureMetrics(double fps, double source_fps, std::uint64_t captured_frames,
                         std::uint64_t dropped_raw_frames, std::uint64_t dropped_encode_frames,
                         std::size_t raw_queue_depth, std::size_t video_queue_depth,
                         double encode_latency_ms);
  void SetRollingMetrics(double duration_seconds, std::size_t bytes, std::size_t packets);
  void SetLastSavedClip(std::filesystem::path path, double duration_ms);
  void SetRecording(bool recording, bool finalizing, std::filesystem::path path = {},
                    double elapsed_seconds = 0.0);
  void SetRecordingMetrics(std::size_t queue_depth, std::uint64_t dropped_packets);
  void SetLastSavedRecording(std::filesystem::path path);
  void SetSettingsStatus(bool restart_required, std::string message);

 private:
  mutable std::mutex mutex_;
  ApplicationSnapshot snapshot_;
};

}  // namespace klip
