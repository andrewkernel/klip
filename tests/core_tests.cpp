#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "klip/core/application_state.h"
#include "klip/core/audio_timeline.h"
#include "klip/core/bounded_queue.h"
#include "klip/core/clip_selection.h"
#include "klip/core/config.h"
#include "klip/core/config_store.h"
#include "klip/core/encoder_selection.h"
#include "klip/core/encoder_tuning.h"
#include "klip/core/video_timeline.h"

namespace {

int failures = 0;

void Check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "FAILED line " << line << ": " << expression << '\n';
    ++failures;
  }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

std::string OptionValue(const klip::EncoderTuning& tuning, const std::string& key) {
  for (const auto& [option_key, value] : tuning.options) {
    if (option_key == key) return value;
  }
  return {};
}

void TestConfig() {
  klip::AppConfig config;
  CHECK(klip::ValidateConfig(config).empty());
  config.clip_duration_seconds = 0.0;
  config.target_fps = 1;
  config.output_width = 1920;
  config.output_height = 0;
  CHECK(klip::ValidateConfig(config).size() >= 3);

  klip::AppConfig high_frame_rate;
  high_frame_rate.target_fps = 120;
  CHECK(!klip::ValidateConfig(high_frame_rate).empty());
  high_frame_rate.target_fps = 144;
  CHECK(!klip::ValidateConfig(high_frame_rate).empty());

  klip::AppConfig invalid_gain;
  invalid_gain.desktop_audio_gain = -0.01;
  invalid_gain.microphone_audio_gain = 2.01;
  CHECK(klip::ValidateConfig(invalid_gain).size() == 2);

  klip::AppConfig invalid_overlay;
  invalid_overlay.static_overlay_x = 0.9;
  invalid_overlay.static_overlay_width = 0.2;
  CHECK(!klip::ValidateConfig(invalid_overlay).empty());
  klip::AppConfig conflicting_overlay;
  conflicting_overlay.static_overlay_enabled = true;
  conflicting_overlay.live_overlay_enabled = true;
  conflicting_overlay.static_overlay_path = "C:/Assets/map-cover.png";
  conflicting_overlay.live_overlay_window_title = "Camera  [WindowsCamera.exe]";
  CHECK(!klip::ValidateConfig(conflicting_overlay).empty());
  klip::AppConfig missing_static_overlay;
  missing_static_overlay.static_overlay_enabled = true;
  CHECK(!klip::ValidateConfig(missing_static_overlay).empty());
  klip::AppConfig missing_live_overlay;
  missing_live_overlay.live_overlay_enabled = true;
  CHECK(!klip::ValidateConfig(missing_live_overlay).empty());

  klip::AppConfig invalid_hotkeys;
  invalid_hotkeys.hotkeys.save_modifiers = klip::HotkeyConfig::kNoRepeat;
  CHECK(!klip::ValidateConfig(invalid_hotkeys).empty());

  klip::AppConfig duplicate_hotkeys;
  duplicate_hotkeys.hotkeys.record_virtual_key = duplicate_hotkeys.hotkeys.save_virtual_key;
  CHECK(!klip::ValidateConfig(duplicate_hotkeys).empty());

  CHECK(klip::FormatHotkey(klip::HotkeyConfig::kControl | klip::HotkeyConfig::kShift,
                           'K') == "ctrl + shift + k");

  klip::AppConfig live_change = klip::AppConfig{};
  live_change.desktop_audio_gain = 0.75;
  live_change.microphone_enabled = true;
  live_change.capture_preview_enabled = true;
  CHECK(!klip::RequiresMediaPipelineReconfigure(klip::AppConfig{}, live_change));

  klip::AppConfig video_change;
  video_change.video_bitrate = 8'000'000;
  CHECK(klip::RequiresMediaPipelineReconfigure(klip::AppConfig{}, video_change));

  klip::AppConfig storage_change;
  storage_change.output_directory = "D:/Klip Clips";
  CHECK(klip::RequiresMediaPipelineReconfigure(klip::AppConfig{}, storage_change));
}

void TestAudioFrameCoverage() {
  constexpr int sample_rate = 48'000;
  constexpr int aac_frame = 1024;
  constexpr std::int64_t start = 1'000'000;
  CHECK(klip::AudioFramesTo100ns(aac_frame, sample_rate) == 213'333);
  CHECK(klip::AudioFramesFrom100nsCeil(100'000, sample_rate) == 480);
  CHECK(!klip::AudioWindowCovered(start, 480, start, aac_frame, sample_rate));
  CHECK(klip::AudioWindowCovered(start, 1024, start, aac_frame, sample_rate));
  CHECK(klip::AudioWindowCovered(start + 150'000, 480, start, aac_frame, sample_rate));
  CHECK(!klip::AudioWindowCovered(start + 50'000, 480, start, aac_frame, sample_rate));
  CHECK(klip::AudioWindowCovered(start + 213'333, 0, start, aac_frame, sample_rate));
}

void TestConfigRoundTrip() {
  auto path = std::filesystem::temp_directory_path() / "klip-core-test-settings.ini";
  klip::AppConfig saved;
  saved.clip_duration_seconds = 42.0;
  saved.target_fps = 30;
  saved.video_bitrate = 8'000'000;
  saved.audio_bitrate = 128'000;
  saved.desktop_audio_enabled = false;
  saved.desktop_audio_gain = 0.65;
  saved.microphone_audio_gain = 1.35;
  saved.rolling_buffer_seconds = 90.0;
  saved.rolling_buffer_bytes = 96ULL * 1024ULL * 1024ULL;
  saved.target_mode = klip::CaptureTargetMode::kDisplay;
  saved.encoder_quality = klip::EncoderQuality::kPerformance;
  saved.scaling_mode = klip::VideoScalingMode::kFit;
  saved.capture_cursor = false;
  saved.capture_border = false;
  saved.capture_preview_enabled = true;
  saved.static_overlay_enabled = true;
  saved.static_overlay_path = "C:/Assets/map-cover.png";
  saved.live_overlay_window_title = "Camera  [WindowsCamera.exe]";
  saved.static_overlay_x = 0.05;
  saved.static_overlay_y = 0.1;
  saved.static_overlay_width = 0.3;
  saved.static_overlay_height = 0.35;
  saved.static_overlay_opacity = 0.8;
  saved.encoder_preferences = {"h264_mf"};
  saved.output_directory = "C:/Videos/Klip Clips";
  saved.preferred_display_name = "Display 1";
  saved.preferred_microphone_name = "Studio Mic";
  saved.excluded_audio_process = "spotify.exe";
  saved.hotkeys.save_modifiers =
      klip::HotkeyConfig::kControl | klip::HotkeyConfig::kShift |
      klip::HotkeyConfig::kNoRepeat;
  saved.hotkeys.save_virtual_key = 'K';
  saved.hotkeys.record_modifiers = klip::HotkeyConfig::kAlt | klip::HotkeyConfig::kNoRepeat;
  saved.hotkeys.record_virtual_key = 0x75;
  saved.hotkeys.toggle_ui_modifiers =
      klip::HotkeyConfig::kControl | klip::HotkeyConfig::kAlt |
      klip::HotkeyConfig::kNoRepeat;
  saved.hotkeys.toggle_ui_virtual_key = 'H';
  std::string diagnostic;
  CHECK(klip::SaveConfig(path, saved, diagnostic));

  klip::AppConfig loaded;
  CHECK(klip::LoadConfig(path, loaded, diagnostic));
  CHECK(loaded.clip_duration_seconds == 42.0);
  CHECK(loaded.target_fps == 30);
  CHECK(loaded.video_bitrate == 8'000'000);
  CHECK(loaded.audio_bitrate == 128'000);
  CHECK(!loaded.desktop_audio_enabled);
  CHECK(loaded.desktop_audio_gain == 0.65);
  CHECK(loaded.microphone_audio_gain == 1.35);
  CHECK(loaded.rolling_buffer_seconds == 90.0);
  CHECK(loaded.rolling_buffer_bytes == 96ULL * 1024ULL * 1024ULL);
  CHECK(loaded.target_mode == klip::CaptureTargetMode::kDisplay);
  CHECK(loaded.encoder_quality == klip::EncoderQuality::kPerformance);
  CHECK(loaded.scaling_mode == klip::VideoScalingMode::kFit);
  CHECK(!loaded.capture_cursor);
  CHECK(!loaded.capture_border);
  CHECK(loaded.capture_preview_enabled);
  CHECK(loaded.static_overlay_enabled);
  CHECK(loaded.static_overlay_path == std::filesystem::path("C:/Assets/map-cover.png"));
  CHECK(loaded.live_overlay_window_title == "Camera  [WindowsCamera.exe]");
  CHECK(loaded.static_overlay_x == 0.05);
  CHECK(loaded.static_overlay_y == 0.1);
  CHECK(loaded.static_overlay_width == 0.3);
  CHECK(loaded.static_overlay_height == 0.35);
  CHECK(loaded.static_overlay_opacity == 0.8);
  CHECK(loaded.encoder_preferences == std::vector<std::string>{"h264_mf"});
  CHECK(loaded.output_directory == std::filesystem::path("C:/Videos/Klip Clips"));
  CHECK(loaded.preferred_display_name == "Display 1");
  CHECK(loaded.preferred_microphone_name == "Studio Mic");
  CHECK(loaded.excluded_audio_process == "spotify.exe");
  CHECK(loaded.hotkeys.save_modifiers == saved.hotkeys.save_modifiers);
  CHECK(loaded.hotkeys.save_virtual_key == 'K');
  CHECK(loaded.hotkeys.record_modifiers == saved.hotkeys.record_modifiers);
  CHECK(loaded.hotkeys.record_virtual_key == 0x75);
  CHECK(loaded.hotkeys.toggle_ui_modifiers == saved.hotkeys.toggle_ui_modifiers);
  CHECK(loaded.hotkeys.toggle_ui_virtual_key == 'H');

  saved.static_overlay_enabled = false;
  saved.live_overlay_enabled = true;
  CHECK(klip::SaveConfig(path, saved, diagnostic));
  klip::AppConfig loaded_live;
  CHECK(klip::LoadConfig(path, loaded_live, diagnostic));
  CHECK(!loaded_live.static_overlay_enabled);
  CHECK(loaded_live.live_overlay_enabled);
  CHECK(loaded_live.live_overlay_window_title == "Camera  [WindowsCamera.exe]");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void TestFrameRatePersistence() {
  const auto path = std::filesystem::temp_directory_path() / "klip-core-test-legacy-fps.ini";
  {
    std::ofstream output(path, std::ios::trunc);
    output << "target_fps=120\n";
    output << "hotkey_modifiers="
           << (klip::HotkeyConfig::kControl | klip::HotkeyConfig::kNoRepeat) << '\n';
  }
  klip::AppConfig loaded;
  std::string diagnostic;
  CHECK(klip::LoadConfig(path, loaded, diagnostic));
  CHECK(loaded.target_fps == 60);
  CHECK(loaded.hotkeys.save_modifiers ==
        (klip::HotkeyConfig::kControl | klip::HotkeyConfig::kNoRepeat));
  CHECK(loaded.hotkeys.record_modifiers == loaded.hotkeys.save_modifiers);
  CHECK(loaded.hotkeys.toggle_ui_modifiers == loaded.hotkeys.save_modifiers);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void TestSpscQueue() {
  klip::SpscQueue<int> queue(2);
  CHECK(queue.TryPush(1));
  CHECK(queue.TryPush(2));
  CHECK(!queue.TryPush(3));
  CHECK(queue.Dropped() == 1);
  int value = 0;
  CHECK(queue.TryPop(value) && value == 1);
  CHECK(queue.TryPop(value) && value == 2);
  CHECK(!queue.TryPop(value));
}

void TestBlockingQueueClosure() {
  klip::BlockingBoundedQueue<int> queue(1);
  CHECK(queue.TryPush(7));
  CHECK(!queue.TryPush(8));
  std::stop_source source;
  int value = 0;
  CHECK(queue.WaitPop(value, source.get_token()) && value == 7);
  queue.Close();
  CHECK(!queue.WaitPop(value, source.get_token()));
  CHECK(queue.Closed());
  queue.Reset();
  CHECK(!queue.Closed());
  CHECK(queue.TryPush(9));
  CHECK(queue.WaitPop(value, source.get_token()) && value == 9);

  klip::BlockingBoundedQueue<int> cancelled(1);
  std::stop_source cancelled_source;
  cancelled_source.request_stop();
  CHECK(!cancelled.WaitPop(value, cancelled_source.get_token()));
}

void TestClipSelection() {
  using klip::PacketDescriptor;
  using klip::StreamKind;
  std::vector<PacketDescriptor> packets{
      {StreamKind::kVideo, 0, 0, 1, 100, true},
      {StreamKind::kAudio, 1, 1, 1, 20, false},
      {StreamKind::kVideo, 20'000'000, 20'000'000, 1, 100, true},
      {StreamKind::kAudio, 20'000'001, 20'000'001, 1, 20, false},
      {StreamKind::kVideo, 40'000'000, 40'000'000, 1, 100, false},
      {StreamKind::kVideo, 60'000'000, 60'000'000, 1, 100, true},
  };
  const auto range = klip::SelectClipRange(packets, 3.0);
  CHECK(range.begin == 2);
  CHECK(range.end == packets.size());
  CHECK(range.base_timestamp_100ns == 20'000'000);
  CHECK(klip::RebaseTimestamp(25, 20) == 5);
  CHECK(klip::RebaseTimestamp(10, 20) == 0);
  CHECK(!klip::ExceedsRollingLimits(0, 50'000'000, 500, 6.0, 1000));
  CHECK(klip::ExceedsRollingLimits(0, 70'000'000, 500, 6.0, 1000));
  CHECK(klip::ExceedsRollingLimits(0, 50'000'000, 1500, 6.0, 1000));
}

void TestEncoderPriority() {
  const std::vector<std::string> configured{"h264_nvenc", "h264_amf", "h264_mf"};
  CHECK(klip::PrioritizeEncoders(configured, 0x1002).front() == "h264_amf");
  CHECK(klip::PrioritizeEncoders(configured, 0x8086).front() == "h264_mf");
  CHECK(klip::PrioritizeEncoders(configured, 0x10DE).front() == "h264_nvenc");
  CHECK(klip::PrioritizeEncoders(configured, 0).front() == "h264_nvenc");
}

void TestVideoTimeline() {
  CHECK(klip::FixedVideoTimestamp100ns(0, 0, 60) == 0);
  CHECK(klip::FixedVideoTimestamp100ns(0, 1, 60) == 166'666);
  CHECK(klip::FixedVideoTimestamp100ns(0, 2, 60) == 333'333);
  CHECK(klip::FixedVideoTimestamp100ns(0, 60, 60) == 10'000'000);
  CHECK(klip::FixedVideoTimestamp100ns(50'000, 1'800, 60) == 300'050'000);
  auto previous = klip::FixedVideoTimestamp100ns(0, 0, 60);
  for (std::uint64_t frame = 1; frame < 3'600; ++frame) {
    const auto current = klip::FixedVideoTimestamp100ns(0, frame, 60);
    CHECK(current > previous);
    CHECK(current - previous == 166'666 || current - previous == 166'667);
    previous = current;
  }
}

void TestEncoderTuning() {
  const auto nvenc_balanced =
      klip::BuildEncoderTuning("h264_nvenc", 60, klip::EncoderQuality::kBalanced);
  CHECK(nvenc_balanced.gop_frames == 120);
  CHECK(nvenc_balanced.max_b_frames == 2);
  CHECK(!nvenc_balanced.low_delay);
  CHECK(OptionValue(nvenc_balanced, "profile") == "high");
  CHECK(OptionValue(nvenc_balanced, "preset") == "p5");
  CHECK(OptionValue(nvenc_balanced, "tune") == "hq");
  CHECK(OptionValue(nvenc_balanced, "multipass") == "qres");
  CHECK(OptionValue(nvenc_balanced, "rc-lookahead") == "8");
  CHECK(OptionValue(nvenc_balanced, "spatial-aq") == "1");
  CHECK(OptionValue(nvenc_balanced, "temporal-aq") == "1");
  CHECK(OptionValue(nvenc_balanced, "zerolatency").empty());

  const auto nvenc_quality =
      klip::BuildEncoderTuning("h264_nvenc", 60, klip::EncoderQuality::kQuality);
  CHECK(OptionValue(nvenc_quality, "preset") == "p6");
  CHECK(OptionValue(nvenc_quality, "multipass") == "fullres");
  CHECK(OptionValue(nvenc_quality, "rc-lookahead") == "16");

  const auto nvenc_performance =
      klip::BuildEncoderTuning("h264_nvenc", 60, klip::EncoderQuality::kPerformance);
  CHECK(nvenc_performance.max_b_frames == 0);
  CHECK(nvenc_performance.low_delay);
  CHECK(OptionValue(nvenc_performance, "tune") == "ll");
  CHECK(OptionValue(nvenc_performance, "zerolatency") == "1");

  const auto amf_balanced =
      klip::BuildEncoderTuning("h264_amf", 60, klip::EncoderQuality::kBalanced);
  CHECK(OptionValue(amf_balanced, "profile") == "high");
  CHECK(OptionValue(amf_balanced, "quality") == "quality");
  CHECK(OptionValue(amf_balanced, "vbaq") == "1");

  const auto mf_quality =
      klip::BuildEncoderTuning("h264_mf", 60, klip::EncoderQuality::kQuality);
  CHECK(OptionValue(mf_quality, "scenario") == "archive");
  CHECK(OptionValue(mf_quality, "quality") == "90");
}

void TestStateSnapshots() {
  klip::ApplicationState state;
  std::atomic<bool> done{false};
  std::jthread writer([&] {
    for (int value = 0; value < 1000; ++value) {
      klip::MetricsSnapshot metrics;
      metrics.captured_frames = static_cast<std::uint64_t>(value);
      metrics.rolling_buffer_packets = static_cast<std::size_t>(value);
      state.SetMetrics(metrics);
    }
    done.store(true, std::memory_order_release);
  });
  while (!done.load(std::memory_order_acquire)) {
    const auto snapshot = state.Snapshot();
    CHECK(snapshot.metrics.captured_frames == snapshot.metrics.rolling_buffer_packets);
  }
}

}  // namespace

int main() {
  TestConfig();
  TestAudioFrameCoverage();
  TestConfigRoundTrip();
  TestFrameRatePersistence();
  TestSpscQueue();
  TestBlockingQueueClosure();
  TestClipSelection();
  TestEncoderPriority();
  TestVideoTimeline();
  TestEncoderTuning();
  TestStateSnapshots();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All Klip core tests passed\n";
  return EXIT_SUCCESS;
}
