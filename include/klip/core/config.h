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

enum class VideoScalingMode {
  kFit,
  kStretch,
};

struct HotkeyConfig {
  bool operator==(const HotkeyConfig&) const = default;
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
  bool obs_replay_enabled = true;
  std::string obs_encoder_id = "auto";
  int obs_cq = 18;
  std::string obs_filename_format = "clip_%CCYY-%MM-%DD_%hh-%mm-%ss";
  bool obs_separate_audio_tracks = false;
  int obs_display_method = 0; // OBS monitor_capture: auto=0, DXGI=1, WGC=2.
  bool obs_limit_game_capture_fps = false; // Opt-in OBS hook copy limiter; never caps the game itself.
  double clip_duration_seconds = 60.0;
  std::uint32_t target_fps = 60;
#if defined(KLIP_USE_LIBOBS)
  std::uint32_t output_width = 1920;
  std::uint32_t output_height = 1080;
#else
  std::uint32_t output_width = 0;   // Zero locks the first target's width for the media pipeline.
  std::uint32_t output_height = 0;  // Zero locks the first target's height for the media pipeline.
#endif
  std::int64_t video_bitrate = 12'000'000;
  std::int64_t audio_bitrate = 192'000;
  bool desktop_audio_enabled = true;
  double desktop_audio_gain = 1.0;
  double microphone_audio_gain = 1.0;
  std::filesystem::path output_directory = "clips";
  std::filesystem::path recording_directory = "recordings";
  std::filesystem::path log_path = "klip.log";
  bool microphone_enabled = false;
  bool capture_cursor = true;
  bool capture_border = true;
  bool capture_preview_enabled = false;
  bool static_overlay_enabled = false;
  std::filesystem::path static_overlay_path;
  bool live_overlay_enabled = false;
  std::string live_overlay_window_title;
  double static_overlay_x = 0.0;
  double static_overlay_y = 0.0;
  double static_overlay_width = 0.25;
  double static_overlay_height = 0.25;
  double static_overlay_opacity = 1.0;
  std::size_t raw_frame_queue_capacity = 4;
  std::size_t encode_queue_capacity = 4;
  std::size_t clip_request_queue_capacity = 8;
  std::size_t recording_packet_queue_capacity = 512;
  double rolling_buffer_seconds = 75.0;
  std::size_t rolling_buffer_bytes = 192ULL * 1024ULL * 1024ULL;
  CaptureTargetMode target_mode = CaptureTargetMode::kGameWindow;
  EncoderQuality encoder_quality = EncoderQuality::kBalanced;
  VideoScalingMode scaling_mode = VideoScalingMode::kStretch;
  std::string preferred_game_title;
  std::string preferred_display_name;
  std::string preferred_microphone_name;
  std::string excluded_audio_process;
  HotkeyConfig hotkeys{};
  std::vector<std::string> encoder_preferences{"h264_nvenc", "h264_amf", "h264_mf"};
};

struct ValidationIssue {
  std::string field;
  std::string message;
};

std::vector<ValidationIssue> ValidateConfig(const AppConfig& config);
std::string FormatHotkey(unsigned int modifiers, unsigned int virtual_key);
bool RequiresMediaPipelineReconfigure(const AppConfig& current, const AppConfig& updated);

}  // namespace klip
