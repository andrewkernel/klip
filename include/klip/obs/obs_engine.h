#pragma once

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "klip/core/application_state.h"
#include "klip/core/logger.h"
#include "klip/platform/output_reservation.h"

struct obs_source;
struct obs_scene;
struct obs_scene_item;
struct obs_encoder;
struct obs_output;
struct obs_volmeter;
struct calldata;

namespace klip {

// libobs owns capture, composition, audio, encoding, and compressed replay packets.
// All command methods run on Klip's UI thread; OBS signals only publish state.
class ObsEngine {
 public:
  ObsEngine(ApplicationState& state, Logger& logger) : state_(state), logger_(logger) {}
  ~ObsEngine() noexcept { Shutdown(); }
  bool Initialize(const AppConfig& config, HWND window, Error& error);
  void Shutdown() noexcept;
  bool ConfigureCapture(CaptureTargetMode mode, const std::string& label, Error& error);
  void ConfigureAudio(const AppConfig& config);
  void SetDashboardActive(bool active);
  bool SaveReplayClip();
  bool StartRecording(Error& error);
  void StopRecording();
  bool IsRecording() const;
  void Tick();
  void RefreshSources();

 private:
  struct SourceChoice { std::uint64_t id; std::string label; std::string value; };
  static void ReplaySaved(void* self, calldata* data) noexcept;
  static void ReplayStopped(void* self, calldata* data) noexcept;
  static void RecordingStopped(void* self, calldata* data) noexcept;
  static void MeterUpdated(void* level, const float* magnitude, const float* peak, const float* input_peak) noexcept;
  bool ConfigureEncoder(Error& error);
  bool ConfigureOverlay(Error& error);
  void UpdateSceneLayout();
  void UpdateAudioMeters();
  bool StartReplayBuffer(Error& error);
  void SetOutputError(obs_output* output, const char* operation,
                      ErrorComponent component = ErrorComponent::kRollingBuffer);
  void StopOutput(obs_output* output) noexcept;
  void ProcessOutputSignals();
  void ProcessSaveQueue();
  void DrainAcceptedSaves() noexcept;

  ApplicationState& state_;
  Logger& logger_;
  AppConfig config_;
  HWND window_ = nullptr;
  std::string core_data_path_;
  bool initialized_ = false;
  bool dashboard_active_ = true;
  bool shutting_down_ = false;
  bool replay_waiting_for_source_ = false;
  bool display_fallback_attempted_ = false;
  bool native_resolution_pending_ = false;
  bool capture_was_ready_ = false;
  std::string pending_game_label_;
  std::string pending_overlay_label_;
  std::chrono::steady_clock::time_point next_source_refresh_{};
  std::chrono::steady_clock::time_point capture_started_{};
  obs_scene* scene_ = nullptr;
  obs_scene_item* capture_item_ = nullptr;
  obs_source* capture_ = nullptr;
  obs_source* overlay_ = nullptr;
  obs_scene_item* overlay_item_ = nullptr;
  std::uint32_t layout_width_ = 0, layout_height_ = 0;
  obs_source* desktop_ = nullptr;
  obs_source* microphone_ = nullptr;
  obs_encoder* video_encoder_ = nullptr;
  obs_encoder* audio_encoder_ = nullptr;
  obs_encoder* desktop_encoder_ = nullptr;
  obs_encoder* microphone_encoder_ = nullptr;
  obs_volmeter* desktop_meter_ = nullptr;
  obs_volmeter* microphone_meter_ = nullptr;
  std::atomic<float> desktop_level_{0.0F}, microphone_level_{0.0F};
  obs_output* replay_ = nullptr;
  obs_output* recording_ = nullptr;
  std::vector<SourceChoice> games_, displays_, microphones_, overlay_windows_;
  std::uint64_t selected_source_id_ = 0;
  std::mutex save_mutex_;
  std::size_t queued_saves_ = 0;
  bool saving_ = false;
  bool save_failed_ = false;
  OutputReservation replay_reservation_;
  OutputReservation recording_reservation_;
  std::chrono::steady_clock::time_point save_started_{};
  std::chrono::steady_clock::time_point replay_started_{};
  std::chrono::steady_clock::time_point recording_started_{};
  std::filesystem::path recording_path_;
  std::atomic<int> replay_stop_code_{0};
  std::atomic<bool> replay_saved_{false};
  // Positive 1 represents a successful stop; OBS errors are negative.
  std::atomic<int> recording_stop_event_{0};
};

}  // namespace klip
