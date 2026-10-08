#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "klip/core/application_state.h"
#include "klip/core/config.h"

namespace klip {

struct CapturePreviewView {
  void* texture = nullptr;
  void* overlay_texture = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct UiCommands {
  std::function<void()> save_clip;
  std::function<void()> toggle_recording;
  std::function<void(CaptureTargetMode)> set_target_mode;
  std::function<void(CaptureTargetMode, std::uint64_t, const std::string&)> select_capture_source;
  std::function<void(bool)> set_capture_border;
  std::function<void(bool)> set_capture_preview_enabled;
  std::function<void(bool)> set_desktop_audio_enabled;
  std::function<void(bool)> set_microphone_enabled;
  std::function<void(float)> set_desktop_audio_gain;
  std::function<void(float)> set_microphone_audio_gain;
  std::function<void()> persist_audio_gains;
  std::function<void(int)> select_microphone;
  std::function<bool(const AppConfig&)> save_settings;
  std::function<void()> open_output_folder;
  std::function<void(const std::filesystem::path&)> open_file;
  std::function<void()> refresh_capture_sources;
};

class MainPanel {
 public:
  void Render(const ApplicationSnapshot& snapshot, const AppConfig& config,
              const UiCommands& commands, bool hotkeys_available,
              const CapturePreviewView& preview);

 private:
  void RenderDashboard(const ApplicationSnapshot& snapshot, const AppConfig& config,
                       const UiCommands& commands, bool hotkeys_available);
  void RenderSettings(const ApplicationSnapshot& snapshot, const AppConfig& config,
                      const UiCommands& commands);
  void RenderCompatibility(const ApplicationSnapshot& snapshot, const AppConfig& config,
                           const UiCommands& commands);
  void RenderDiagnosticsPreview(const ApplicationSnapshot& snapshot, const AppConfig& config);
  void UpdateSetupTest(const ApplicationSnapshot& snapshot, const UiCommands& commands);
  void RenderCapturePreview(const CapturePreviewView& preview, const AppConfig& config);
  void ResetDraft(const AppConfig& config);

  AppConfig draft_;
  std::array<char, 512> clips_path_{};
  std::array<char, 512> recordings_path_{};
  std::array<char, 512> overlay_path_{};
  std::array<char, 256> obs_filename_format_{};
  bool settings_open_ = false;
  int requested_settings_tab_ = -1;
  bool draft_initialized_ = false;
  bool diagnostics_preview_open_ = false;
  bool diagnostics_include_private_details_ = false;
  bool setup_test_requested_ = false;
  bool setup_test_recording_started_ = false;
  bool setup_test_stop_sent_ = false;
  bool desktop_gain_dirty_ = false;
  bool microphone_gain_dirty_ = false;
  std::filesystem::path last_clip_seen_;
  std::chrono::steady_clock::time_point clip_toast_until_{};
  std::chrono::steady_clock::time_point diagnostics_copied_until_{};
  std::chrono::steady_clock::time_point setup_test_requested_at_{};
  std::chrono::steady_clock::time_point setup_test_stop_at_{};
  std::filesystem::path setup_test_previous_recording_;
  std::filesystem::path compatibility_storage_path_;
  std::filesystem::path compatibility_recording_storage_path_;
  std::chrono::steady_clock::time_point compatibility_storage_check_after_{};
  std::uint64_t compatibility_free_bytes_ = 0;
  std::uint64_t compatibility_recording_free_bytes_ = 0;
  bool compatibility_storage_available_ = false;
  bool compatibility_recording_storage_available_ = false;
  std::string setup_test_status_;
  int hotkey_capture_target_ = -1;
  std::string hotkey_capture_message_;
};

}  // namespace klip
