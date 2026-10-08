#include <atomic>
#include <chrono>
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
#include "klip/core/path_text.h"
#include "klip/core/video_timeline.h"
#include "klip/platform/dashboard_activity.h"
#include "klip/obs/encoder_policy.h"

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
  auto obs_invalid = config;
  obs_invalid.obs_cq = 52;
  obs_invalid.obs_filename_format = "../clip";
  obs_invalid.obs_display_method = 3;
  CHECK(klip::ValidateConfig(obs_invalid).size() == 3);
  config.clip_duration_seconds = 0.0;
  config.target_fps = 1;
  config.output_width = 1920;
  config.output_height = 0;
  CHECK(klip::ValidateConfig(config).size() >= 3);

  klip::AppConfig high_frame_rate;
  high_frame_rate.target_fps = 120;
  CHECK(klip::ValidateConfig(high_frame_rate).empty());
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

  CHECK(klip::FormatHotkey(klip::HotkeyConfig::kControl | klip::HotkeyConfig::kShift, 'K') ==
        "ctrl + shift + k");

  klip::AppConfig live_change = klip::AppConfig{};
  live_change.desktop_audio_gain = 0.75;
  live_change.desktop_audio_enabled = false;
  live_change.microphone_enabled = true;
  live_change.capture_preview_enabled = true;
  CHECK(!klip::RequiresMediaPipelineReconfigure(klip::AppConfig{}, live_change));

  klip::AppConfig video_change;
  video_change.video_bitrate = 8'000'000;
  CHECK(klip::RequiresMediaPipelineReconfigure(klip::AppConfig{}, video_change));

  klip::AppConfig storage_change;
  storage_change.output_directory = "D:/Klip Clips";
  CHECK(klip::RequiresMediaPipelineReconfigure(klip::AppConfig{}, storage_change));
  klip::AppConfig capture_limiter;
  CHECK(!capture_limiter.obs_limit_game_capture_fps);
  capture_limiter.obs_limit_game_capture_fps = true;
  CHECK(klip::RequiresMediaPipelineReconfigure(klip::AppConfig{}, capture_limiter));
}

void TestPathToUtf8() {
  const auto path = std::filesystem::path(u8"C:/Users/André/Vidéos/ゲーム/clip.mp4");
  CHECK(klip::PathToUtf8(path) ==
        "C:/Users/Andr\xc3\xa9/Vid\xc3\xa9os/\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0/clip.mp4");
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
  saved.obs_replay_enabled = false;
  saved.obs_encoder_id = "obs_nvenc_h264_tex";
  saved.obs_cq = 21;
  saved.obs_display_method = 2;
  saved.obs_limit_game_capture_fps = true;
  saved.obs_filename_format = "klip_%CCYY-%MM-%DD_%hh-%mm-%ss";
  saved.obs_separate_audio_tracks = true;
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
  saved.static_overlay_path = std::filesystem::path(u8"C:/Users/André/Assets/ゲーム-map.png");
  saved.live_overlay_window_title = "Camera  [WindowsCamera.exe]";
  saved.static_overlay_x = 0.05;
  saved.static_overlay_y = 0.1;
  saved.static_overlay_width = 0.3;
  saved.static_overlay_height = 0.35;
  saved.static_overlay_opacity = 0.8;
  saved.encoder_preferences = {"h264_mf"};
  saved.output_directory = std::filesystem::path(u8"C:/Users/André/Vidéos/Klip Clips");
  saved.recording_directory = std::filesystem::path(u8"C:/Users/André/Vidéos/録画");
  saved.preferred_display_name = "Display 1";
  saved.preferred_microphone_name = "Studio Mic";
  saved.excluded_audio_process = "spotify.exe";
  saved.hotkeys.save_modifiers =
      klip::HotkeyConfig::kControl | klip::HotkeyConfig::kShift | klip::HotkeyConfig::kNoRepeat;
  saved.hotkeys.save_virtual_key = 'K';
  saved.hotkeys.record_modifiers = klip::HotkeyConfig::kAlt | klip::HotkeyConfig::kNoRepeat;
  saved.hotkeys.record_virtual_key = 0x75;
  saved.hotkeys.toggle_ui_modifiers =
      klip::HotkeyConfig::kControl | klip::HotkeyConfig::kAlt | klip::HotkeyConfig::kNoRepeat;
  saved.hotkeys.toggle_ui_virtual_key = 'H';
  std::string diagnostic;
  CHECK(klip::SaveConfig(path, saved, diagnostic));

  klip::AppConfig loaded;
  CHECK(klip::LoadConfig(path, loaded, diagnostic));
  CHECK(!loaded.obs_replay_enabled);
  CHECK(loaded.obs_encoder_id == saved.obs_encoder_id);
  CHECK(loaded.obs_cq == 21 && loaded.obs_display_method == 2);
  CHECK(loaded.obs_limit_game_capture_fps);
  CHECK(loaded.obs_filename_format == saved.obs_filename_format);
  CHECK(loaded.obs_separate_audio_tracks);
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
  CHECK(loaded.static_overlay_path ==
        std::filesystem::path(u8"C:/Users/André/Assets/ゲーム-map.png"));
  CHECK(loaded.live_overlay_window_title == "Camera  [WindowsCamera.exe]");
  CHECK(loaded.static_overlay_x == 0.05);
  CHECK(loaded.static_overlay_y == 0.1);
  CHECK(loaded.static_overlay_width == 0.3);
  CHECK(loaded.static_overlay_height == 0.35);
  CHECK(loaded.static_overlay_opacity == 0.8);
  CHECK(loaded.encoder_preferences == std::vector<std::string>{"h264_mf"});
  CHECK(loaded.output_directory ==
        std::filesystem::path(u8"C:/Users/André/Vidéos/Klip Clips"));
  CHECK(loaded.recording_directory == std::filesystem::path(u8"C:/Users/André/Vidéos/録画"));
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
    output << "hotkey_modifiers=" << (klip::HotkeyConfig::kControl | klip::HotkeyConfig::kNoRepeat)
           << '\n';
  }
  klip::AppConfig loaded;
  std::string diagnostic;
  CHECK(klip::LoadConfig(path, loaded, diagnostic));
  CHECK(loaded.target_fps == 120);
  CHECK(loaded.hotkeys.save_modifiers ==
        (klip::HotkeyConfig::kControl | klip::HotkeyConfig::kNoRepeat));
  CHECK(loaded.hotkeys.record_modifiers == loaded.hotkeys.save_modifiers);
  CHECK(loaded.hotkeys.toggle_ui_modifiers == loaded.hotkeys.save_modifiers);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void TestLegacyObsEncoderMigration() {
  const auto path = std::filesystem::temp_directory_path() / "klip-core-test-encoder-migration.ini";
  const auto check = [&](const std::string& text, const std::string& expected) {
    { std::ofstream output(path, std::ios::trunc); output << text; }
    klip::AppConfig loaded;
    std::string diagnostic;
    CHECK(klip::LoadConfig(path, loaded, diagnostic));
    CHECK(loaded.obs_encoder_id == expected);
  };
  check("encoder_preferences=h264_nvenc\n", "obs_nvenc_h264_tex");
  check("encoder_preferences=h264_amf\n", "h264_texture_amf");
  check("encoder_preferences=h264_qsv\n", "obs_qsv11_v2");
  check("encoder_preferences=libx264\n", "obs_x264");
  check("encoder_preferences=unsupported_encoder\n", "unsupported_encoder");
  check("encoder_preferences=h264_nvenc,h264_amf,h264_mf\n", "auto");
  check("obs_encoder_id=\"auto\"\nencoder_preferences=h264_nvenc\n", "auto");
  check("encoder_preferences=h264_nvenc\nobs_encoder_id=\"obs_x264\"\n", "obs_x264");
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
  CHECK((klip::FilterEncodersForAdapter(configured, 0x10DE) ==
         std::vector<std::string>{"h264_nvenc", "h264_mf"}));
  CHECK((klip::FilterEncodersForAdapter(configured, 0x1002) ==
         std::vector<std::string>{"h264_amf", "h264_mf"}));
  CHECK((klip::FilterEncodersForAdapter(configured, 0x8086) ==
         std::vector<std::string>{"h264_mf"}));
  CHECK(klip::FilterEncodersForAdapter(configured, 0) == configured);

  const auto failure = klip::FormatEncoderSelectionFailure(
      {"h264_nvenc: driver refused the D3D11 device", "h264_mf_software: transform missing"});
  CHECK(failure.find("h264_nvenc") != std::string::npos);
  CHECK(failure.find("transform missing") != std::string::npos);
  CHECK(failure.find("Windows N") != std::string::npos);
  CHECK(failure.find("Media Feature Pack") != std::string::npos);
  const auto summarized = klip::FormatEncoderSelectionFailure({std::string(1200, 'x')});
  CHECK(summarized.find("additional encoder errors are in klip.log") != std::string::npos);
  CHECK(summarized.size() < 1000);
}

void TestVideoTimeline() {
  CHECK(klip::EvenNv12Dimension(1) == 2);
  CHECK(klip::EvenNv12Dimension(720) == 720);
  CHECK(klip::EvenNv12Dimension(1281) == 1280);
  CHECK(klip::EvenNv12Dimension(8192) == 8192);
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

  CHECK(klip::CaptureFrameIsDue(0, -1, 60));
  CHECK(klip::CaptureFrameIsDue(165'000, 0, 60));
  CHECK(klip::CaptureFrameIsDue(333'000, 165'000, 60));
  CHECK(!klip::CaptureFrameIsDue(70'000, 0, 60));
  CHECK(!klip::CaptureFrameIsDue(0, 0, 60));
  CHECK(klip::CaptureFrameIsDue(83'000, 0, 120));

  int recycled_stale_frame = 0;
  CHECK(!klip::RecycleIfCaptureGenerationStale(7, 7, [&] { ++recycled_stale_frame; }));
  CHECK(klip::RecycleIfCaptureGenerationStale(7, 8, [&] { ++recycled_stale_frame; }));
  CHECK(recycled_stale_frame == 1);

  // A 60 Hz source with +/-1 ms arrival jitter should not be halved by the
  // capture gate. The old exact-interval cutoff dropped every early frame.
  std::int64_t accepted_at = -1;
  int accepted = 0;
  for (int frame = 0; frame < 120; ++frame) {
    const auto timestamp =
        static_cast<std::int64_t>(frame) * 166'667 + (frame % 2 == 0 ? 10'000 : -10'000);
    if (klip::CaptureFrameIsDue(timestamp, accepted_at, 60)) {
      accepted_at = timestamp;
      ++accepted;
    }
  }
  CHECK(accepted == 120);

  // At 120 FPS, a 240 Hz WGC compositor clock has a 4.167 ms interval. Callback
  // delivery jitter of +/-0.2 ms can straddle the 4.167 ms admission threshold,
  // while SystemRelativeTime remains stable. Gate on source time, not dispatch.
  std::int64_t accepted_source_time = -1;
  std::int64_t accepted_callback_time = -1;
  int source_clock_accepted = 0;
  int callback_clock_accepted = 0;
  for (int frame = 0; frame < 120; ++frame) {
    const auto source_time = static_cast<std::int64_t>(frame) * 41'667;
    const auto callback_time = source_time + (frame % 2 == 0 ? 2'000 : -2'000);
    if (klip::CaptureFrameIsDue(source_time, accepted_source_time, 120)) {
      accepted_source_time = source_time;
      ++source_clock_accepted;
    }
    if (klip::CaptureFrameIsDue(callback_time, accepted_callback_time, 120)) {
      accepted_callback_time = callback_time;
      ++callback_clock_accepted;
    }
  }
  CHECK(source_clock_accepted == 120);
  CHECK(callback_clock_accepted < source_clock_accepted);
}

void TestSourceAlignedSampling() {
  // A source near the requested output rate can deliver alternating early/late
  // frames. Future samples must survive until their own output slot.
  for (const auto fps : {30U, 60U, 120U}) {
    const std::int64_t origin = 1'234'567;
    klip::VideoCadenceTracker cadence(fps);
    for (std::uint64_t frame = 0; frame <= 16; ++frame)
      cadence.Observe(klip::FixedVideoTimestamp100ns(origin, frame, fps));
    CHECK(cadence.MatchesOutput());
    const auto faster_origin = klip::FixedVideoTimestamp100ns(origin, 16, fps);
    for (std::uint64_t frame = 1; frame <= 16; ++frame)
      cadence.Observe(klip::FixedVideoTimestamp100ns(faster_origin, frame, fps * 2));
    CHECK(!cadence.MatchesOutput());
    cadence.Reset();
    CHECK(!cadence.MatchesOutput());
    // Slow encoder initialization must not leave a permanent queue backlog.
    const auto now = origin + 1'234'567;
    const auto aligned = klip::AlignedVideoOrigin100ns(origin, now, fps);
    CHECK(aligned <= now);
    CHECK(now - aligned <= klip::FixedVideoTimestamp100ns(0, 1, fps) + 1);
    CHECK(klip::AlignedVideoOrigin100ns(-1, now, fps) == now);
    std::vector<std::int64_t> source;
    for (std::uint64_t frame = 0; frame < 240; ++frame) {
      const auto jitter = frame == 0 ? 0 : (frame % 2 ? 1 : -1) * (2'000'000LL / fps);
      source.push_back(klip::FixedVideoTimestamp100ns(origin, frame, fps) + jitter);
    }
    std::size_t pending = 0;
    for (std::uint64_t frame = 0; frame < source.size(); ++frame) {
      const auto output = klip::FixedVideoTimestamp100ns(origin, frame, fps);
      std::size_t selected = source.size();
      while (pending < source.size() && klip::VideoFrameIsDueForOutput(source[pending], output, fps))
        selected = pending++;
      CHECK(selected == frame);
    }
    // A genuinely slower source is repeated, not assigned to an earlier slot.
    const auto slower_next = klip::FixedVideoTimestamp100ns(origin, 2, fps);
    CHECK(!klip::VideoFrameIsDueForOutput(slower_next,
                                        klip::FixedVideoTimestamp100ns(origin, 1, fps), fps));
    CHECK(klip::VideoFrameIsDueForOutput(slower_next, slower_next, fps));
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
  CHECK(OptionValue(nvenc_performance, "surfaces") == "16");
  CHECK(OptionValue(nvenc_performance, "zerolatency") == "1");

  const auto amf_balanced =
      klip::BuildEncoderTuning("h264_amf", 60, klip::EncoderQuality::kBalanced);
  CHECK(OptionValue(amf_balanced, "profile") == "high");
  CHECK(OptionValue(amf_balanced, "quality") == "quality");
  CHECK(OptionValue(amf_balanced, "vbaq") == "1");

  const auto amf_performance =
      klip::BuildEncoderTuning("h264_amf", 120, klip::EncoderQuality::kPerformance);
  CHECK(amf_performance.gop_frames == 240);
  CHECK(amf_performance.low_delay);
  CHECK(OptionValue(amf_performance, "usage") == "transcoding");
  CHECK(OptionValue(amf_performance, "quality") == "speed");
  CHECK(OptionValue(amf_performance, "latency") == "1");
  CHECK(OptionValue(amf_performance, "async_depth") == "2");

  const auto mf_quality = klip::BuildEncoderTuning("h264_mf", 60, klip::EncoderQuality::kQuality);
  CHECK(OptionValue(mf_quality, "scenario") == "archive");
  CHECK(OptionValue(mf_quality, "quality") == "90");

  const auto mf_software_balanced =
      klip::BuildEncoderTuning("h264_mf_software", 60, klip::EncoderQuality::kBalanced);
  CHECK(mf_software_balanced.max_b_frames == 0);
  CHECK(mf_software_balanced.low_delay);
  CHECK(OptionValue(mf_software_balanced, "scenario") == "archive");
  CHECK(OptionValue(mf_software_balanced, "quality") == "80");
}

void TestQueueWakeups() {
  using namespace std::chrono_literals;
  klip::SpscQueue<int> queue(1);
  std::atomic<int> received{0};
  std::atomic<bool> ordered{true};
  std::jthread consumer([&](std::stop_token stop) {
    int value = 0;
    while (queue.WaitPop(value, stop)) {
      if (value != received.load()) ordered.store(false);
      received.fetch_add(1);
    }
  });
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  for (int value = 0; value < 10000; ++value) {
    while (!queue.TryPush(value) && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    while (received.load() <= value && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    if (std::chrono::steady_clock::now() >= deadline) break;
  }
  consumer.request_stop();
  consumer.join();
  CHECK(received.load() == 10000);
  CHECK(ordered.load());
  CHECK(queue.Size() == 0);

  // Cancellation must wake an empty queue even when it races with entering wait.
  for (int iteration = 0; iteration < 100; ++iteration) {
    klip::BlockingBoundedQueue<int> blocking(1);
    std::jthread waiter([&](std::stop_token stop) {
      int value;
      blocking.WaitPop(value, stop);
    });
    waiter.request_stop();
  }
}

void TestStateSnapshots() {
  klip::ApplicationState state;
  state.SetGraphicsAdapter("test adapter", 0x10de, "11.1");
  state.SetCaptureAdapter("display adapter", "cross-adapter");
  state.SetEncoder("h264_mf_software");
  state.SetEncoderStatus("software compatibility fallback active");
  auto initial = state.Snapshot();
  CHECK(initial.graphics_adapter == "test adapter");
  CHECK(initial.graphics_adapter_vendor_id == 0x10de);
  CHECK(initial.graphics_feature_level == "11.1");
  CHECK(initial.capture_adapter == "display adapter");
  CHECK(initial.capture_adapter_relationship == "cross-adapter");
  CHECK(initial.selected_encoder == "h264_mf_software");
  CHECK(initial.encoder_status == "software compatibility fallback active");
  state.SetEncoderStatus({});
  CHECK(state.Snapshot().encoder_status.empty());
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

void TestErrorOwnershipAndGeneration() {
  klip::ApplicationState state;
  state.SetError(klip::Error{klip::ErrorComponent::kAudio, "device", "audio failed"});
  CHECK(state.Snapshot().error_generation == 1);
  state.SetError(klip::Error{klip::ErrorComponent::kApplication, "settings", "save failed"});
  const auto second = state.Snapshot();
  CHECK(second.error_generation == 2);
  CHECK(second.last_error && second.last_error->component == klip::ErrorComponent::kApplication);
  state.ClearError(klip::ErrorComponent::kAudio);
  CHECK(state.Snapshot().last_error.has_value());
  state.ClearError(klip::ErrorComponent::kApplication);
  CHECK(!state.Snapshot().last_error.has_value());
  CHECK(state.Snapshot().error_generation == 2);
}

}  // namespace

int main() {
  const auto balanced_obs = klip::ObsNvencPolicyFor(klip::EncoderQuality::kBalanced);
  CHECK(std::string(balanced_obs.preset) == "p5" && std::string(balanced_obs.multipass) == "qres" && balanced_obs.adaptive_quantization);
  const auto performance_obs = klip::ObsNvencPolicyFor(klip::EncoderQuality::kPerformance);
  CHECK(std::string(performance_obs.preset) == "p3" && std::string(performance_obs.multipass) == "disabled" && !performance_obs.adaptive_quantization);
  const auto maximum_obs = klip::ObsNvencPolicyFor(klip::EncoderQuality::kQuality);
  CHECK(std::string(maximum_obs.preset) == "p7" && std::string(maximum_obs.multipass) == "fullres" && maximum_obs.adaptive_quantization);
  CHECK(klip::DashboardWorkEnabled(true, false));
  CHECK(!klip::DashboardWorkEnabled(true, true));
  CHECK(!klip::DashboardWorkEnabled(false, false));
  CHECK(!klip::DashboardWorkEnabled(false, true));
  TestConfig();
  TestPathToUtf8();
  TestAudioFrameCoverage();
  TestConfigRoundTrip();
  TestFrameRatePersistence();
  TestLegacyObsEncoderMigration();
  TestSpscQueue();
  TestBlockingQueueClosure();
  TestQueueWakeups();
  TestClipSelection();
  TestEncoderPriority();
  TestVideoTimeline();
  TestEncoderTuning();
  TestSourceAlignedSampling();
  TestStateSnapshots();
  TestErrorOwnershipAndGeneration();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All Klip core tests passed\n";
  return EXIT_SUCCESS;
}
