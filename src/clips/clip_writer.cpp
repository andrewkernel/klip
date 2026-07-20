#include "klip/clips/clip_writer.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <exception>

#include "klip/media/mp4_muxer.h"

extern "C" {
#include <libavutil/mathematics.h>
}

namespace klip {
namespace {

constexpr AVRational kHundredNanoseconds{1, 10'000'000};

}  // namespace

ClipWriter::ClipWriter(RollingMediaBuffer& buffer, ApplicationState& state, Logger& logger,
                       std::size_t queue_capacity)
    : buffer_(buffer), state_(state), logger_(logger), requests_(queue_capacity) {}

ClipWriter::~ClipWriter() noexcept { Stop(); }

bool ClipWriter::Start(AppConfig config, SnapshotProvider video, SnapshotProvider audio,
                       Error& error) {
  if (running_.exchange(true, std::memory_order_acq_rel)) return true;
  config_ = std::move(config);
  video_snapshot_ = std::move(video);
  audio_snapshot_ = std::move(audio);
  std::error_code directory_error;
  std::filesystem::create_directories(config_.output_directory, directory_error);
  if (directory_error) {
    running_.store(false, std::memory_order_release);
    error = Error{ErrorComponent::kClipWriter, "create output directory",
                  directory_error.message(),   directory_error.value(),
                  directory_error.message(),   config_.output_directory.string()};
    return false;
  }
  worker_ = std::jthread([this](std::stop_token token) { Worker(token); });
  return true;
}

void ClipWriter::Stop() noexcept {
  if (!running_.exchange(false, std::memory_order_acq_rel)) return;
  requests_.Close();
  worker_.request_stop();
  worker_ = {};
  video_snapshot_ = {};
  audio_snapshot_ = {};
}

bool ClipWriter::RequestClip() {
  if (!running_.load(std::memory_order_acquire)) return false;
  Request request{BuildOutputPath(config_.output_directory), config_.clip_duration_seconds};
  const auto accepted = requests_.TryPush(std::move(request));
  if (accepted)
    logger_.Info("Clip save requested");
  else
    logger_.Warning("Clip save request rejected: writer queue is full or closed");
  return accepted;
}

void ClipWriter::Worker(std::stop_token stop_token) noexcept {
  try {
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    Request request;
    while (requests_.WaitPop(request, stop_token)) {
      const auto previous_state = state_.Snapshot();
      const auto restore_status = [&] {
        if (state_.Snapshot().status == CaptureStatus::kSaving)
          state_.SetStatus(previous_state.status, previous_state.current_operation);
      };
      state_.SetStatus(CaptureStatus::kSaving, "Saving clip");
      const auto started = std::chrono::steady_clock::now();
      CodecSnapshot video;
      CodecSnapshot audio;
      Error error;
      if (!video_snapshot_ || !video_snapshot_(video)) {
        error = Error{ErrorComponent::kClipWriter, "snapshot video codec",
                      "video encoder is not ready"};
      } else {
        const auto has_audio = audio_snapshot_ && audio_snapshot_(audio);
        // A clip requested immediately after capture starts can arrive before the
        // encoder has emitted its first keyframe. Give the rolling buffer a short
        // warm-up window instead of failing the user-visible clip operation.
        auto packets = buffer_.Snapshot(request.duration_seconds);
        constexpr auto kKeyframeWarmup = std::chrono::seconds(5);
        const auto warmup_deadline = std::chrono::steady_clock::now() + kKeyframeWarmup;
        while (packets.empty() && !stop_token.stop_requested() &&
               std::chrono::steady_clock::now() < warmup_deadline) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          packets = buffer_.Snapshot(request.duration_seconds);
        }
        if (packets.empty()) {
          error = Error{ErrorComponent::kClipWriter, "select clip range",
                        "the rolling buffer does not yet contain a keyframe"};
        } else if (Write(request, packets, video, has_audio ? &audio : nullptr, error)) {
          const auto duration =
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                  .count();
          state_.SetLastSavedClip(request.output_path, duration);
          state_.ClearError();
          logger_.Info("Clip saved: " + request.output_path.string());
          restore_status();
          continue;
        }
      }
      state_.SetError(error);
      logger_.ErrorMessage(error);
      restore_status();
    }
  } catch (const std::exception& exception) {
    Error error{ErrorComponent::kClipWriter, "clip writer worker", exception.what()};
    state_.SetError(error);
    logger_.ErrorMessage(error);
  }
}

bool ClipWriter::Write(const Request& request, const std::vector<EncodedPacket>& packets,
                       const CodecSnapshot& video, const CodecSnapshot* audio, Error& error) {
  std::int64_t base = INT64_MAX;
  for (const auto& encoded : packets) {
    if (!encoded.packet) continue;
    const auto timestamp =
        encoded.packet->dts == AV_NOPTS_VALUE ? encoded.packet->pts : encoded.packet->dts;
    if (timestamp != AV_NOPTS_VALUE) {
      base = std::min(base, av_rescale_q(timestamp, encoded.time_base, kHundredNanoseconds));
    }
  }
  if (base == INT64_MAX) base = 0;

  Mp4Muxer muxer;
  if (!muxer.Open(request.output_path, video, audio, ErrorComponent::kClipWriter, error))
    return false;
  for (const auto& encoded : packets) {
    if (!muxer.Write(encoded, base, error)) return false;
  }
  return muxer.Finalize(error);
}

std::filesystem::path ClipWriter::BuildOutputPath(const std::filesystem::path& directory) {
  SYSTEMTIME time{};
  GetLocalTime(&time);
  wchar_t filename[128]{};
  swprintf_s(filename, L"clip_%04u%02u%02u_%02u%02u%02u_%03u.mp4", time.wYear, time.wMonth,
             time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
  return directory / filename;
}

}  // namespace klip
