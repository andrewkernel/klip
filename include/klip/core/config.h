#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace klip {

enum class CaptureTargetMode {
  kGameWindow,
  kDisplay,
  kActiveWindow = kGameWindow,
  kFullDesktop = kDisplay,
};

enum class EncoderQuality {
  kPerformance,
  kBalanced,
  kQuality,
};

struct HotkeyConfig {
  static constexpr unsigned int kAlt = 0x0001;
  static constexpr unsigned int kControl = 0x0002;
  static constexpr unsigned int kShift = 0x0004;
  static constexpr unsigned int kWindows = 0x0008;
  static constexpr unsigned int kNoRepeat = 0x4000;

  unsigned int save_modifiers = kAlt | kNoRepeat;
  unsigned int save_virtual_key = 'C';
  unsigned int record_modifiers = kAlt | kNoRepeat;
  unsigned int record_virtual_key = 'R';
  unsigned int toggle_ui_modifiers = kAlt | kNoRepeat;
  unsigned int toggle_ui_virtual_key = 'X';
};

struct AppConfig {
  double clip_duration_seconds = 60.0;
  std::uint32_t target_fps = 60;
  std::uint32_t output_width = 0;   // Zero preserves the capture target width.
  std::uint32_t output_height = 0;  // Zero preserves the capture target height.
  std::int64_t video_bitrate = 12'000'000;
  std::int64_t audio_bitrate = 192'000;
  double desktop_audio_gain = 1.0;
  double microphone_audio_gain = 1.0;
  std::filesystem::path output_directory = "clips";
  std::filesystem::path recording_directory = "recordings";
  std::filesystem::path log_path = "klip.log";
  bool microphone_enabled = false;
  bool capture_cursor = true;
  bool capture_border = true;
  std::size_t raw_frame_queue_capacity = 4;
  std::size_t encode_queue_capacity = 4;
  std::size_t clip_request_queue_capacity = 8;
  std::size_t recording_packet_queue_capacity = 512;
  double rolling_buffer_seconds = 75.0;
  std::size_t rolling_buffer_bytes = 192ULL * 1024ULL * 1024ULL;
  CaptureTargetMode target_mode = CaptureTargetMode::kGameWindow;
  EncoderQuality encoder_quality = EncoderQuality::kBalanced;
  std::string preferred_game_title;
  std::string preferred_display_name;
  std::string preferred_microphone_name;
  HotkeyConfig hotkeys{};
  std::vector<std::string> encoder_preferences{"h264_nvenc", "h264_amf", "h264_mf"};
};

struct ValidationIssue {
  std::string field;
  std::string message;
};

std::vector<ValidationIssue> ValidateConfig(const AppConfig& config);
std::string FormatHotkey(unsigned int modifiers, unsigned int virtual_key);

}  // namespace klip
