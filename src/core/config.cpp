#include "klip/core/config.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace klip {
namespace {

constexpr unsigned int kAllowedHotkeyModifiers =
    HotkeyConfig::kAlt | HotkeyConfig::kControl | HotkeyConfig::kShift |
    HotkeyConfig::kWindows | HotkeyConfig::kNoRepeat;
constexpr unsigned int kChordModifiers = kAllowedHotkeyModifiers & ~HotkeyConfig::kNoRepeat;

bool SameHotkey(unsigned int modifiers_a, unsigned int key_a, unsigned int modifiers_b,
                unsigned int key_b) {
  return (modifiers_a & kChordModifiers) == (modifiers_b & kChordModifiers) && key_a == key_b;
}

}  // namespace

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
  const auto normalized = [](double value) {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
  };
  if (!normalized(config.static_overlay_x))
    reject("static_overlay_x", "must be between 0.0 and 1.0");
  if (!normalized(config.static_overlay_y))
    reject("static_overlay_y", "must be between 0.0 and 1.0");
  if (!normalized(config.static_overlay_width) || config.static_overlay_width <= 0.0)
    reject("static_overlay_width", "must be greater than 0.0 and no more than 1.0");
  if (!normalized(config.static_overlay_height) || config.static_overlay_height <= 0.0)
    reject("static_overlay_height", "must be greater than 0.0 and no more than 1.0");
  if (!normalized(config.static_overlay_opacity))
    reject("static_overlay_opacity", "must be between 0.0 and 1.0");
  if (config.static_overlay_enabled && config.live_overlay_enabled)
    reject("overlay_source", "static image and live window cannot both be enabled");
  if (config.static_overlay_enabled && config.static_overlay_path.empty())
    reject("static_overlay_path", "choose a PNG, JPG, or BMP image");
  if (config.live_overlay_enabled && config.live_overlay_window_title.empty())
    reject("live_overlay_window_title", "choose a live overlay window");
  if (config.static_overlay_x + config.static_overlay_width > 1.0)
    reject("static_overlay_width", "overlay must stay inside the output width");
  if (config.static_overlay_y + config.static_overlay_height > 1.0)
    reject("static_overlay_height", "overlay must stay inside the output height");
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

  if ((config.hotkeys.save_modifiers & ~kAllowedHotkeyModifiers) != 0 ||
      (config.hotkeys.record_modifiers & ~kAllowedHotkeyModifiers) != 0 ||
      (config.hotkeys.toggle_ui_modifiers & ~kAllowedHotkeyModifiers) != 0) {
    reject("hotkeys", "contains unsupported modifier flags");
  }
  if ((config.hotkeys.save_modifiers & kChordModifiers) == 0 ||
      (config.hotkeys.record_modifiers & kChordModifiers) == 0 ||
      (config.hotkeys.toggle_ui_modifiers & kChordModifiers) == 0) {
    reject("hotkeys", "each shortcut must include Ctrl, Alt, Shift, or Windows");
  }
  if (config.hotkeys.save_virtual_key == 0 || config.hotkeys.save_virtual_key > 0xFE ||
      config.hotkeys.record_virtual_key == 0 || config.hotkeys.record_virtual_key > 0xFE ||
      config.hotkeys.toggle_ui_virtual_key == 0 ||
      config.hotkeys.toggle_ui_virtual_key > 0xFE) {
    reject("hotkeys", "shortcut keys must be valid Windows virtual keys");
  }
  if (SameHotkey(config.hotkeys.save_modifiers, config.hotkeys.save_virtual_key,
                 config.hotkeys.record_modifiers, config.hotkeys.record_virtual_key) ||
      SameHotkey(config.hotkeys.save_modifiers, config.hotkeys.save_virtual_key,
                 config.hotkeys.toggle_ui_modifiers, config.hotkeys.toggle_ui_virtual_key) ||
      SameHotkey(config.hotkeys.record_modifiers, config.hotkeys.record_virtual_key,
                 config.hotkeys.toggle_ui_modifiers, config.hotkeys.toggle_ui_virtual_key)) {
    reject("hotkeys", "clip, recording, and hide shortcuts must be different");
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

std::string FormatHotkey(unsigned int modifiers, unsigned int virtual_key) {
  std::string text;
  const auto add = [&text](const char* value) {
    if (!text.empty()) text += " + ";
    text += value;
  };
  if ((modifiers & HotkeyConfig::kControl) != 0) add("ctrl");
  if ((modifiers & HotkeyConfig::kAlt) != 0) add("alt");
  if ((modifiers & HotkeyConfig::kShift) != 0) add("shift");
  if ((modifiers & HotkeyConfig::kWindows) != 0) add("win");

  if (virtual_key >= 'A' && virtual_key <= 'Z') {
    const char key[] = {static_cast<char>(virtual_key - 'A' + 'a'), '\0'};
    add(key);
  } else if (virtual_key >= '0' && virtual_key <= '9') {
    const char key[] = {static_cast<char>(virtual_key), '\0'};
    add(key);
  } else if (virtual_key >= 0x70 && virtual_key <= 0x87) {
    const auto key = "f" + std::to_string(virtual_key - 0x6F);
    add(key.c_str());
  } else if (virtual_key >= 0x60 && virtual_key <= 0x69) {
    const auto key = "numpad " + std::to_string(virtual_key - 0x60);
    add(key.c_str());
  } else {
    const char* name = nullptr;
    switch (virtual_key) {
      case 0x08: name = "backspace"; break;
      case 0x09: name = "tab"; break;
      case 0x0D: name = "enter"; break;
      case 0x13: name = "pause"; break;
      case 0x14: name = "caps lock"; break;
      case 0x20: name = "space"; break;
      case 0x21: name = "page up"; break;
      case 0x22: name = "page down"; break;
      case 0x23: name = "end"; break;
      case 0x24: name = "home"; break;
      case 0x25: name = "left"; break;
      case 0x26: name = "up"; break;
      case 0x27: name = "right"; break;
      case 0x28: name = "down"; break;
      case 0x2D: name = "insert"; break;
      case 0x2E: name = "delete"; break;
      case 0x2C: name = "print screen"; break;
      case 0x6A: name = "numpad *"; break;
      case 0x6B: name = "numpad +"; break;
      case 0x6D: name = "numpad -"; break;
      case 0x6E: name = "numpad ."; break;
      case 0x6F: name = "numpad /"; break;
      case 0x90: name = "num lock"; break;
      case 0x91: name = "scroll lock"; break;
      case 0xBA: name = ";"; break;
      case 0xBB: name = "="; break;
      case 0xBC: name = ","; break;
      case 0xBD: name = "-"; break;
      case 0xBE: name = "."; break;
      case 0xBF: name = "/"; break;
      case 0xC0: name = "`"; break;
      case 0xDB: name = "["; break;
      case 0xDC: name = "\\"; break;
      case 0xDD: name = "]"; break;
      case 0xDE: name = "'"; break;
      default: break;
    }
    if (name != nullptr) add(name);
    else {
      std::ostringstream key;
      key << "key 0x" << std::hex << virtual_key;
      add(key.str().c_str());
    }
  }
  return text;
}

bool RequiresMediaPipelineReconfigure(const AppConfig& current, const AppConfig& updated) {
  return current.target_fps != updated.target_fps ||
         current.output_width != updated.output_width ||
         current.output_height != updated.output_height ||
         current.scaling_mode != updated.scaling_mode ||
         current.video_bitrate != updated.video_bitrate ||
         current.audio_bitrate != updated.audio_bitrate ||
         current.capture_cursor != updated.capture_cursor ||
         current.target_mode != updated.target_mode ||
         current.encoder_quality != updated.encoder_quality ||
         current.static_overlay_enabled != updated.static_overlay_enabled ||
         current.static_overlay_path != updated.static_overlay_path ||
         current.live_overlay_enabled != updated.live_overlay_enabled ||
         current.live_overlay_window_title != updated.live_overlay_window_title ||
         current.static_overlay_x != updated.static_overlay_x ||
         current.static_overlay_y != updated.static_overlay_y ||
         current.static_overlay_width != updated.static_overlay_width ||
         current.static_overlay_height != updated.static_overlay_height ||
         current.static_overlay_opacity != updated.static_overlay_opacity ||
         current.preferred_game_title != updated.preferred_game_title ||
         current.preferred_display_name != updated.preferred_display_name ||
         current.encoder_preferences != updated.encoder_preferences ||
         current.clip_duration_seconds != updated.clip_duration_seconds ||
         current.rolling_buffer_seconds != updated.rolling_buffer_seconds ||
         current.rolling_buffer_bytes != updated.rolling_buffer_bytes ||
         current.output_directory != updated.output_directory ||
         current.recording_directory != updated.recording_directory ||
         current.raw_frame_queue_capacity != updated.raw_frame_queue_capacity ||
         current.encode_queue_capacity != updated.encode_queue_capacity ||
         current.recording_packet_queue_capacity != updated.recording_packet_queue_capacity;
}

}  // namespace klip
