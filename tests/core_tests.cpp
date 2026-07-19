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

namespace {

int failures = 0;

void Check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "FAILED line " << line << ": " << expression << '\n';
    ++failures;
  }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

void TestConfig() {
  klip::AppConfig config;
  CHECK(klip::ValidateConfig(config).empty());
  config.clip_duration_seconds = 0.0;
  config.target_fps = 1;
  config.output_width = 1920;
  config.output_height = 0;
  CHECK(klip::ValidateConfig(config).size() >= 3);

  klip::AppConfig unsupported_frame_rate;
  unsupported_frame_rate.target_fps = 120;
  CHECK(!klip::ValidateConfig(unsupported_frame_rate).empty());

  klip::AppConfig invalid_gain;
  invalid_gain.desktop_audio_gain = -0.01;
  invalid_gain.microphone_audio_gain = 2.01;
  CHECK(klip::ValidateConfig(invalid_gain).size() == 2);
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
  saved.desktop_audio_gain = 0.65;
  saved.microphone_audio_gain = 1.35;
  saved.rolling_buffer_seconds = 90.0;
  saved.rolling_buffer_bytes = 96ULL * 1024ULL * 1024ULL;
  saved.target_mode = klip::CaptureTargetMode::kDisplay;
  saved.encoder_quality = klip::EncoderQuality::kPerformance;
  saved.capture_cursor = false;
  saved.encoder_preferences = {"h264_mf"};
  saved.output_directory = "C:/Videos/Klip Clips";
  saved.preferred_display_name = "Display 1";
  saved.preferred_microphone_name = "Studio Mic";
  std::string diagnostic;
  CHECK(klip::SaveConfig(path, saved, diagnostic));

  klip::AppConfig loaded;
  CHECK(klip::LoadConfig(path, loaded, diagnostic));
  CHECK(loaded.clip_duration_seconds == 42.0);
  CHECK(loaded.target_fps == 30);
  CHECK(loaded.video_bitrate == 8'000'000);
  CHECK(loaded.audio_bitrate == 128'000);
  CHECK(loaded.desktop_audio_gain == 0.65);
  CHECK(loaded.microphone_audio_gain == 1.35);
  CHECK(loaded.rolling_buffer_seconds == 90.0);
  CHECK(loaded.rolling_buffer_bytes == 96ULL * 1024ULL * 1024ULL);
  CHECK(loaded.target_mode == klip::CaptureTargetMode::kDisplay);
  CHECK(loaded.encoder_quality == klip::EncoderQuality::kPerformance);
  CHECK(!loaded.capture_cursor);
  CHECK(loaded.encoder_preferences == std::vector<std::string>{"h264_mf"});
  CHECK(loaded.output_directory == std::filesystem::path("C:/Videos/Klip Clips"));
  CHECK(loaded.preferred_display_name == "Display 1");
  CHECK(loaded.preferred_microphone_name == "Studio Mic");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void TestLegacyFrameRateMigration() {
  const auto path = std::filesystem::temp_directory_path() / "klip-core-test-legacy-fps.ini";
  {
    std::ofstream output(path, std::ios::trunc);
    output << "target_fps=120\n";
  }
  klip::AppConfig loaded;
  std::string diagnostic;
  CHECK(klip::LoadConfig(path, loaded, diagnostic));
  CHECK(loaded.target_fps == 60);
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
  TestLegacyFrameRateMigration();
  TestSpscQueue();
  TestBlockingQueueClosure();
  TestClipSelection();
  TestEncoderPriority();
  TestStateSnapshots();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All Klip core tests passed\n";
  return EXIT_SUCCESS;
}
