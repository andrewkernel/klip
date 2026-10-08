#include "klip/core/application_state.h"

#include <utility>

namespace klip {

ApplicationSnapshot ApplicationState::Snapshot() const {
  std::scoped_lock lock(mutex_);
  return snapshot_;
}

void ApplicationState::Update(const ApplicationSnapshot& snapshot) {
  std::scoped_lock lock(mutex_);
  snapshot_ = snapshot;
}

void ApplicationState::SetStatus(CaptureStatus status, std::string operation) {
  std::scoped_lock lock(mutex_);
  snapshot_.status = status;
  snapshot_.current_operation = std::move(operation);
}

void ApplicationState::SetError(Error error) {
  std::scoped_lock lock(mutex_);
  snapshot_.last_error = std::move(error);
  ++snapshot_.error_generation;
}

void ApplicationState::ClearError(ErrorComponent component) {
  std::scoped_lock lock(mutex_);
  if (snapshot_.last_error && snapshot_.last_error->component == component)
    snapshot_.last_error.reset();
}

void ApplicationState::SetTarget(CaptureTargetMode mode, std::string description,
                                 std::uint64_t source_id) {
  std::scoped_lock lock(mutex_);
  snapshot_.target_mode = mode;
  snapshot_.capture_target = std::move(description);
  snapshot_.selected_capture_source_id = source_id;
}

void ApplicationState::SetCaptureSources(std::vector<CaptureSourceOption> game_sources,
                                         std::vector<CaptureSourceOption> display_sources,
                                         std::uint64_t selected_source_id) {
  std::scoped_lock lock(mutex_);
  snapshot_.game_sources = std::move(game_sources);
  snapshot_.display_sources = std::move(display_sources);
  snapshot_.selected_capture_source_id = selected_source_id;
}

void ApplicationState::SetGraphicsAdapter(std::string adapter, std::uint32_t vendor_id,
                                          std::string feature_level) {
  std::scoped_lock lock(mutex_);
  snapshot_.graphics_adapter = std::move(adapter);
  snapshot_.graphics_adapter_vendor_id = vendor_id;
  snapshot_.graphics_feature_level = std::move(feature_level);
}

void ApplicationState::SetOverlaySources(std::vector<CaptureSourceOption> sources) {
  std::scoped_lock lock(mutex_);
  snapshot_.overlay_sources = std::move(sources);
}

void ApplicationState::SetCaptureAdapter(std::string adapter, std::string relationship) {
  std::scoped_lock lock(mutex_);
  snapshot_.capture_adapter = std::move(adapter);
  snapshot_.capture_adapter_relationship = std::move(relationship);
}

void ApplicationState::SetAvailableEncoders(std::vector<std::string> encoders) {
  std::scoped_lock lock(mutex_);
  snapshot_.available_encoders = std::move(encoders);
}

void ApplicationState::SetEncoder(std::string encoder) {
  std::scoped_lock lock(mutex_);
  snapshot_.selected_encoder = std::move(encoder);
}

void ApplicationState::SetEncoderStatus(std::string status) {
  std::scoped_lock lock(mutex_);
  snapshot_.encoder_status = std::move(status);
}

void ApplicationState::SetAudio(bool desktop_active, bool microphone_active, float desktop_level,
                                float microphone_level) {
  std::scoped_lock lock(mutex_);
  snapshot_.desktop_audio_active = desktop_active;
  snapshot_.microphone_active = microphone_active;
  snapshot_.desktop_audio_level = desktop_level;
  snapshot_.microphone_level = microphone_level;
}

void ApplicationState::SetMicrophoneEnabled(bool enabled) {
  std::scoped_lock lock(mutex_);
  snapshot_.microphone_enabled = enabled;
}

void ApplicationState::SetMicrophones(std::vector<std::string> microphones, int selected_index) {
  std::scoped_lock lock(mutex_);
  snapshot_.microphones = std::move(microphones);
  snapshot_.selected_microphone_index = selected_index;
}

void ApplicationState::SetAudioApplications(std::vector<AudioApplicationOption> applications) {
  std::scoped_lock lock(mutex_);
  snapshot_.audio_applications = std::move(applications);
}

void ApplicationState::SetMetrics(const MetricsSnapshot& metrics) {
  std::scoped_lock lock(mutex_);
  snapshot_.metrics = metrics;
}

void ApplicationState::SetCaptureMetrics(double fps, double source_fps,
                                         std::uint64_t captured_frames,
                                         std::uint64_t dropped_raw_frames,
                                         std::uint64_t dropped_encode_frames,
                                         std::size_t raw_queue_depth, std::size_t video_queue_depth,
                                         double encode_latency_ms) {
  std::scoped_lock lock(mutex_);
  snapshot_.metrics.capture_fps = fps;
  snapshot_.metrics.source_fps = source_fps;
  snapshot_.metrics.captured_frames = captured_frames;
  snapshot_.metrics.dropped_raw_frames = dropped_raw_frames;
  snapshot_.metrics.dropped_encode_frames = dropped_encode_frames;
  snapshot_.metrics.raw_queue_depth = raw_queue_depth;
  snapshot_.metrics.video_queue_depth = video_queue_depth;
  snapshot_.metrics.encode_latency_ms = encode_latency_ms;
}

void ApplicationState::SetRollingMetrics(double duration_seconds, std::size_t bytes,
                                         std::size_t packets) {
  std::scoped_lock lock(mutex_);
  snapshot_.metrics.rolling_buffer_seconds = duration_seconds;
  snapshot_.metrics.rolling_buffer_bytes = bytes;
  snapshot_.metrics.rolling_buffer_packets = packets;
}

void ApplicationState::SetLastSavedClip(std::filesystem::path path, double duration_ms) {
  std::scoped_lock lock(mutex_);
  snapshot_.last_saved_clip = std::move(path);
  snapshot_.metrics.last_clip_save_ms = duration_ms;
}

void ApplicationState::SetRecording(bool recording, bool finalizing, std::filesystem::path path,
                                    double elapsed_seconds) {
  std::scoped_lock lock(mutex_);
  snapshot_.recording = recording;
  snapshot_.finalizing_recording = finalizing;
  snapshot_.recording_path = std::move(path);
  snapshot_.recording_seconds = elapsed_seconds;
}

void ApplicationState::SetRecordingMetrics(std::size_t queue_depth, std::uint64_t dropped_packets) {
  std::scoped_lock lock(mutex_);
  snapshot_.metrics.recording_queue_depth = queue_depth;
  snapshot_.metrics.dropped_recording_packets = dropped_packets;
}

void ApplicationState::SetLastSavedRecording(std::filesystem::path path) {
  std::scoped_lock lock(mutex_);
  snapshot_.last_saved_recording = std::move(path);
}

void ApplicationState::SetSettingsStatus(bool restart_required, std::string message) {
  std::scoped_lock lock(mutex_);
  snapshot_.settings_restart_required = restart_required;
  snapshot_.settings_message = std::move(message);
}

}  // namespace klip
