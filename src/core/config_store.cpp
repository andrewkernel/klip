#include "klip/core/config_store.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace klip {
namespace {

std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

template <typename T>
bool ParseInteger(std::string_view value, T& output) {
  T parsed{};
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || end != value.data() + value.size()) return false;
  output = parsed;
  return true;
}

bool ParseDouble(std::string_view value, double& output) {
  std::istringstream stream{std::string(value)};
  double parsed = 0.0;
  stream >> parsed;
  if (!stream || !stream.eof()) return false;
  output = parsed;
  return true;
}

bool ParseBool(std::string_view value, bool& output) {
  if (value == "true" || value == "1") {
    output = true;
    return true;
  }
  if (value == "false" || value == "0") {
    output = false;
    return true;
  }
  return false;
}

std::string ParseString(const std::string& value) {
  std::istringstream stream(value);
  std::string parsed;
  if (stream >> std::quoted(parsed)) return parsed;
  return value;
}

std::filesystem::path ParsePath(const std::string& value) {
  const auto text = ParseString(value);
  std::u8string utf8;
  utf8.reserve(text.size());
  for (const auto byte : text)
    utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
  return std::filesystem::path(utf8);
}

std::string PathText(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

std::vector<std::string> SplitEncoders(const std::string& value) {
  std::vector<std::string> encoders;
  std::istringstream stream(value);
  std::string encoder;
  while (std::getline(stream, encoder, ',')) {
    encoder = Trim(std::move(encoder));
    if (!encoder.empty()) encoders.push_back(std::move(encoder));
  }
  return encoders;
}

std::string JoinEncoders(const std::vector<std::string>& encoders) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < encoders.size(); ++index) {
    if (index != 0) stream << ',';
    stream << encoders[index];
  }
  return stream.str();
}

const char* ModeName(CaptureTargetMode mode) {
  return mode == CaptureTargetMode::kDisplay ? "display" : "game_window";
}

const char* QualityName(EncoderQuality quality) {
  switch (quality) {
    case EncoderQuality::kPerformance: return "performance";
    case EncoderQuality::kBalanced: return "balanced";
    case EncoderQuality::kQuality: return "quality";
  }
  return "balanced";
}

bool AssignValue(AppConfig& config, const std::string& key, const std::string& value) {
  if (key == "clip_duration_seconds") return ParseDouble(value, config.clip_duration_seconds);
  if (key == "target_fps") {
    std::uint32_t parsed = 0;
    if (!ParseInteger(value, parsed)) return false;
    config.target_fps = parsed == 120 ? 60 : parsed;
    return true;
  }
  if (key == "output_width") return ParseInteger(value, config.output_width);
  if (key == "output_height") return ParseInteger(value, config.output_height);
  if (key == "video_bitrate") return ParseInteger(value, config.video_bitrate);
  if (key == "audio_bitrate") return ParseInteger(value, config.audio_bitrate);
  if (key == "desktop_audio_gain") return ParseDouble(value, config.desktop_audio_gain);
  if (key == "microphone_audio_gain") return ParseDouble(value, config.microphone_audio_gain);
  if (key == "rolling_buffer_seconds") return ParseDouble(value, config.rolling_buffer_seconds);
  if (key == "rolling_buffer_bytes") return ParseInteger(value, config.rolling_buffer_bytes);
  if (key == "microphone_enabled") return ParseBool(value, config.microphone_enabled);
  if (key == "capture_cursor") return ParseBool(value, config.capture_cursor);
  if (key == "capture_border") return ParseBool(value, config.capture_border);
  if (key == "output_directory") {
    config.output_directory = ParsePath(value);
    return true;
  }
  if (key == "recording_directory") {
    config.recording_directory = ParsePath(value);
    return true;
  }
  if (key == "preferred_game_title") {
    config.preferred_game_title = ParseString(value);
    return true;
  }
  if (key == "preferred_display_name") {
    config.preferred_display_name = ParseString(value);
    return true;
  }
  if (key == "preferred_microphone_name") {
    config.preferred_microphone_name = ParseString(value);
    return true;
  }
  if (key == "capture_mode") {
    if (value == "display") config.target_mode = CaptureTargetMode::kDisplay;
    if (value == "game_window" || value == "active_window")
      config.target_mode = CaptureTargetMode::kGameWindow;
    return value == "display" || value == "game_window" || value == "active_window";
  }
  if (key == "encoder_quality") {
    if (value == "performance") config.encoder_quality = EncoderQuality::kPerformance;
    if (value == "balanced") config.encoder_quality = EncoderQuality::kBalanced;
    if (value == "quality") config.encoder_quality = EncoderQuality::kQuality;
    return value == "performance" || value == "balanced" || value == "quality";
  }
  if (key == "encoder_preferences") {
    auto encoders = SplitEncoders(value);
    if (encoders.empty()) return false;
    for (auto& encoder : encoders) {
      if (encoder == "h264_qsv") encoder = "h264_mf";
    }
    std::vector<std::string> unique;
    for (auto& encoder : encoders) {
      if (std::find(unique.begin(), unique.end(), encoder) == unique.end())
        unique.push_back(std::move(encoder));
    }
    config.encoder_preferences = std::move(unique);
    return true;
  }
  return true;  // Forward-compatible: ignore settings from newer versions.
}

}  // namespace

bool LoadConfig(const std::filesystem::path& path, AppConfig& config, std::string& diagnostic) {
  diagnostic.clear();
  std::ifstream input(path);
  if (!input) {
    if (!std::filesystem::exists(path)) return true;
    diagnostic = "Could not open settings file: " + path.string();
    return false;
  }

  AppConfig loaded = config;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    line = Trim(std::move(line));
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      diagnostic = "Invalid settings line " + std::to_string(line_number);
      return false;
    }
    const auto key = Trim(line.substr(0, separator));
    const auto value = Trim(line.substr(separator + 1));
    if (!AssignValue(loaded, key, value)) {
      diagnostic = "Invalid value for '" + key + "' on settings line " +
                   std::to_string(line_number);
      return false;
    }
  }

  const auto issues = ValidateConfig(loaded);
  if (!issues.empty()) {
    diagnostic = "Invalid saved setting '" + issues.front().field + "': " +
                 issues.front().message;
    return false;
  }
  config = std::move(loaded);
  return true;
}

bool SaveConfig(const std::filesystem::path& path, const AppConfig& config,
                std::string& diagnostic) {
  diagnostic.clear();
  const auto issues = ValidateConfig(config);
  if (!issues.empty()) {
    diagnostic = "Invalid setting '" + issues.front().field + "': " + issues.front().message;
    return false;
  }

  std::error_code filesystem_error;
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path(), filesystem_error);
  if (filesystem_error) {
    diagnostic = "Could not create settings directory: " + filesystem_error.message();
    return false;
  }

  auto temporary = path;
  temporary += ".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  if (!output) {
    diagnostic = "Could not write settings file: " + temporary.string();
    return false;
  }
  output << "# Klip settings - changes made in the app are saved here.\n";
  output << "clip_duration_seconds=" << config.clip_duration_seconds << '\n';
  output << "target_fps=" << config.target_fps << '\n';
  output << "output_width=" << config.output_width << '\n';
  output << "output_height=" << config.output_height << '\n';
  output << "video_bitrate=" << config.video_bitrate << '\n';
  output << "audio_bitrate=" << config.audio_bitrate << '\n';
  output << "desktop_audio_gain=" << config.desktop_audio_gain << '\n';
  output << "microphone_audio_gain=" << config.microphone_audio_gain << '\n';
  output << "rolling_buffer_seconds=" << config.rolling_buffer_seconds << '\n';
  output << "rolling_buffer_bytes=" << config.rolling_buffer_bytes << '\n';
  output << "microphone_enabled=" << (config.microphone_enabled ? "true" : "false") << '\n';
  output << "capture_cursor=" << (config.capture_cursor ? "true" : "false") << '\n';
  output << "capture_border=" << (config.capture_border ? "true" : "false") << '\n';
  output << "capture_mode=" << ModeName(config.target_mode) << '\n';
  output << "encoder_quality=" << QualityName(config.encoder_quality) << '\n';
  output << "encoder_preferences=" << JoinEncoders(config.encoder_preferences) << '\n';
  output << "output_directory=" << std::quoted(PathText(config.output_directory)) << '\n';
  output << "recording_directory=" << std::quoted(PathText(config.recording_directory)) << '\n';
  output << "preferred_game_title=" << std::quoted(config.preferred_game_title) << '\n';
  output << "preferred_display_name=" << std::quoted(config.preferred_display_name) << '\n';
  output << "preferred_microphone_name=" << std::quoted(config.preferred_microphone_name)
         << '\n';
  output.flush();
  if (!output) {
    diagnostic = "Could not finish writing settings file: " + temporary.string();
    return false;
  }
  output.close();

  filesystem_error.clear();
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    filesystem_error = std::error_code(static_cast<int>(GetLastError()),
                                       std::system_category());
  }
#else
  std::filesystem::rename(temporary, path, filesystem_error);
#endif
  if (filesystem_error) {
    diagnostic = "Could not publish settings file: " + filesystem_error.message();
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  return true;
}

}  // namespace klip
