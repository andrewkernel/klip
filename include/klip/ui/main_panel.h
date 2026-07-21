#pragma once

#include <array>
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
  std::function<void(CaptureTargetMode, std::uint64_t, const std::string&)>
      select_capture_source;
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
  void RenderCapturePreview(const CapturePreviewView& preview, const AppConfig& config);
  void ResetDraft(const AppConfig& config);

  AppConfig draft_;
  std::array<char, 512> clips_path_{};
  std::array<char, 512> recordings_path_{};
  std::array<char, 512> overlay_path_{};
  bool settings_open_ = false;
  bool draft_initialized_ = false;
  bool desktop_gain_dirty_ = false;
  bool microphone_gain_dirty_ = false;
  int hotkey_capture_target_ = -1;
  std::string hotkey_capture_message_;
};

}  // namespace klip
