#include "klip/recording/recording_writer.h"

#include <Windows.h>

#include <exception>
#include <utility>

#include "klip/media/mp4_muxer.h"
#include "klip/core/path_text.h"

namespace klip {

RecordingWriter::RecordingWriter(ApplicationState& state, Logger& logger)
    : state_(state), logger_(logger) {}

RecordingWriter::~RecordingWriter() noexcept { Shutdown(); }

bool RecordingWriter::Initialize(AppConfig config, SnapshotProvider video, SnapshotProvider audio,
                                 Error& error) {
  Shutdown();
  config_ = std::move(config);
  video_snapshot_ = std::move(video);
  audio_snapshot_ = std::move(audio);
  std::error_code directory_error;
  std::filesystem::create_directories(config_.recording_directory, directory_error);
  if (directory_error) {
    error = Error{ErrorComponent::kRecordingWriter, "create recording directory",
                  directory_error.message(),        directory_error.value(),
                  directory_error.message(),        PathToUtf8(config_.recording_directory)};
    return false;
  }
  initialized_.store(true, std::memory_order_release);
  return true;
}

void RecordingWriter::Shutdown() noexcept {
  StopRecording();
  initialized_.store(false, std::memory_order_release);
  video_snapshot_ = {};
  audio_snapshot_ = {};
}

bool RecordingWriter::StartRecording(Error& error) {
  if (recording_.load(std::memory_order_acquire)) return true;
  if (!initialized_.load(std::memory_order_acquire)) {
    error = Error{ErrorComponent::kRecordingWriter, "start recording",
                  "the recording writer is not initialized"};
    return false;
  }
  if (worker_.joinable()) worker_ = {};

  CodecSnapshot video;
  CodecSnapshot audio;
  if (!video_snapshot_ || !video_snapshot_(video)) {
    error = Error{ErrorComponent::kRecordingWriter, "snapshot video codec",
                  "capture is still warming up; try recording again in a moment"};
    return false;
  }
  const bool has_audio = audio_snapshot_ && audio_snapshot_(audio);

  std::scoped_lock lock(control_mutex_);
  if (recording_.load(std::memory_order_acquire)) return true;
  active_path_ = BuildOutputPath(config_.recording_directory);
  active_queue_ = std::make_shared<PacketQueue>(config_.recording_packet_queue_capacity);
  dropped_packets_.store(0, std::memory_order_release);
  started_at_ = std::chrono::steady_clock::now();
  stop_requested_at_ = {};
  stop_video_packet_target_ = 0;
  stop_audio_packet_target_ = 0;
  graceful_stop_requested_ = false;
  recording_.store(true, std::memory_order_release);
  finalizing_.store(false, std::memory_order_release);
  state_.SetRecording(true, false, active_path_, 0.0);
  logger_.Info("Recording started: " + PathToUtf8(active_path_));

  auto queue = active_queue_;
  auto path = active_path_;
  worker_ = std::jthread([this, queue = std::move(queue), path = std::move(path),
                          video = std::move(video), audio = std::move(audio),
                          has_audio](std::stop_token) mutable {
    Worker(std::move(queue), std::move(path), std::move(video), std::move(audio), has_audio);
  });
  return true;
}

void RecordingWriter::RequestStopRecording(std::uint64_t video_packet_target,
                                           std::uint64_t audio_packet_target,
                                           std::uint64_t video_packets_routed,
                                           std::uint64_t audio_packets_routed) noexcept {
  std::filesystem::path path;
  double elapsed = 0.0;
  bool should_finalize = false;
  {
    std::scoped_lock lock(control_mutex_);
    if (!recording_.load(std::memory_order_acquire) || graceful_stop_requested_) return;
    graceful_stop_requested_ = true;
    stop_requested_at_ = std::chrono::steady_clock::now();
    stop_video_packet_target_ = video_packet_target;
    stop_audio_packet_target_ = audio_packet_target;
    path = active_path_;
    elapsed = std::chrono::duration<double>(stop_requested_at_ - started_at_).count();
    should_finalize = video_packets_routed >= stop_video_packet_target_ &&
                      audio_packets_routed >= stop_audio_packet_target_;
    if (!should_finalize) {
      finalizing_.store(true, std::memory_order_release);
      state_.SetRecording(true, true, path, elapsed);
      logger_.Info("Recording stop requested; waiting for delayed encoder packets");
    }
  }
  if (should_finalize) CloseQueueForFinalization(false);
}

void RecordingWriter::StopRecording() noexcept {
  std::shared_ptr<PacketQueue> queue;
  std::filesystem::path path;
  {
    std::scoped_lock lock(control_mutex_);
    if (!recording_.exchange(false, std::memory_order_acq_rel) && !worker_.joinable()) return;
    finalizing_.store(true, std::memory_order_release);
    graceful_stop_requested_ = false;
    queue = active_queue_;
    path = active_path_;
    if (queue) queue->Close();
  }
  state_.SetRecording(false, true, path, 0.0);
  if (worker_.joinable()) worker_ = {};
  {
    std::scoped_lock lock(control_mutex_);
    active_queue_.reset();
    active_path_.clear();
  }
  finalizing_.store(false, std::memory_order_release);
  state_.SetRecording(false, false);
}

void RecordingWriter::Publish(const AVPacket* packet, StreamKind kind, AVRational time_base,
                              std::uint64_t video_packets_routed,
                              std::uint64_t audio_packets_routed) noexcept {
  if (packet == nullptr || !recording_.load(std::memory_order_acquire)) return;
  PacketPtr clone(av_packet_clone(packet));
  bool drain_complete = false;
  {
    std::scoped_lock lock(control_mutex_);
    if (!recording_.load(std::memory_order_acquire) || !active_queue_) return;
    if (!clone || !active_queue_->TryPush(EncodedPacket{std::move(clone), kind, time_base})) {
      // Dropping compressed reference pictures and continuing corrupts subsequent
      // pictures. Drain only the accepted prefix; replay capture remains independent.
      dropped_packets_.fetch_add(1, std::memory_order_relaxed);
      recording_.store(false, std::memory_order_release);
      finalizing_.store(true, std::memory_order_release);
      graceful_stop_requested_ = false;
      const Error error{ErrorComponent::kRecordingWriter, "queue encoded packet",
                        "recording stopped because storage or memory could not keep up; "
                        "saving the accepted portion. Replay capture continues"};
      state_.SetError(error);
      state_.SetRecording(false, true, active_path_, 0.0);
      logger_.ErrorMessage(error);
      active_queue_->Close();
    } else {
      drain_complete = graceful_stop_requested_ &&
                       video_packets_routed >= stop_video_packet_target_ &&
                       audio_packets_routed >= stop_audio_packet_target_;
    }
  }
  if (drain_complete) CloseQueueForFinalization(false);
}

void RecordingWriter::CloseQueueForFinalization(bool drain_timeout) noexcept {
  std::shared_ptr<PacketQueue> queue;
  std::filesystem::path path;
  {
    std::scoped_lock lock(control_mutex_);
    if (!recording_.load(std::memory_order_acquire) || !graceful_stop_requested_ ||
        !active_queue_)
      return;
    queue = active_queue_;
    path = active_path_;
    recording_.store(false, std::memory_order_release);
    finalizing_.store(true, std::memory_order_release);
    graceful_stop_requested_ = false;
    queue->Close();
  }
  state_.SetRecording(false, true, path, 0.0);
  if (drain_timeout) {
    const Error error{ErrorComponent::kRecordingWriter, "drain encoder packets",
                      "timed out waiting for delayed encoder output; the recording may be short"};
    state_.SetError(error);
    logger_.ErrorMessage(error);
  } else {
    logger_.Info("Recording encoder backlog drained; finalizing MP4");
  }
}

void RecordingWriter::Tick() {
  std::shared_ptr<PacketQueue> queue;
  std::filesystem::path path;
  bool active = false;
  bool finishing = false;
  double seconds = 0.0;
  bool drain_timeout = false;
  {
    std::scoped_lock lock(control_mutex_);
    queue = active_queue_;
    path = active_path_;
    active = recording_.load(std::memory_order_acquire);
    finishing = finalizing_.load(std::memory_order_acquire);
    if (active) {
      const auto now = std::chrono::steady_clock::now();
      seconds = std::chrono::duration<double>(now - started_at_).count();
      drain_timeout = graceful_stop_requested_ && now - stop_requested_at_ >=
                                                       std::chrono::seconds(2);
      state_.SetRecording(true, finishing, path, seconds);
    }
  }
  state_.SetRecordingMetrics(queue ? queue->Size() : 0,
                             dropped_packets_.load(std::memory_order_relaxed));
  if (drain_timeout) CloseQueueForFinalization(true);
}

void RecordingWriter::Worker(std::shared_ptr<PacketQueue> queue, std::filesystem::path output_path,
                             CodecSnapshot video, CodecSnapshot audio, bool has_audio) noexcept {
  try {
    // Background mode also lowers I/O priority, which can starve a live writer.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    Mp4Muxer muxer;
    Error error;
    if (!muxer.Open(output_path, video, has_audio ? &audio : nullptr,
                    ErrorComponent::kRecordingWriter, error)) {
      recording_.store(false, std::memory_order_release);
      finalizing_.store(false, std::memory_order_release);
      state_.SetError(error);
      state_.SetRecording(false, false);
      logger_.ErrorMessage(error);
      return;
    }

    bool started = false;
    bool write_ok = true;
    std::int64_t base_timestamp = 0;
    EncodedPacket encoded;
    const std::stop_token never_stop;
    while (queue->WaitPop(encoded, never_stop)) {
      const auto descriptor = DescribePacket(encoded);
      if (!started) {
        if (descriptor.kind != StreamKind::kVideo || !descriptor.keyframe) continue;
        base_timestamp = descriptor.dts_100ns;
        started = true;
      }
      if (descriptor.dts_100ns < base_timestamp) continue;
      if (!muxer.Write(encoded, base_timestamp, error)) {
        write_ok = false;
        break;
      }
    }

    if (!started) {
      error = Error{ErrorComponent::kRecordingWriter, "start MP4 timeline",
                    "recording ended before the encoder produced a keyframe"};
      write_ok = false;
    }
    if (write_ok) write_ok = muxer.Finalize(error);
    if (write_ok) {
      state_.SetLastSavedRecording(output_path);
      if (dropped_packets_.load(std::memory_order_relaxed) == 0)
        state_.ClearError(ErrorComponent::kRecordingWriter);
      logger_.Info("Recording saved: " + PathToUtf8(output_path));
    } else {
      state_.SetError(error);
      logger_.ErrorMessage(error);
    }
    recording_.store(false, std::memory_order_release);
  } catch (const std::exception& exception) {
    const Error error{ErrorComponent::kRecordingWriter, "recording worker", exception.what()};
    recording_.store(false, std::memory_order_release);
    state_.SetError(error);
    logger_.ErrorMessage(error);
  }
  finalizing_.store(false, std::memory_order_release);
  state_.SetRecording(false, false);
}

std::filesystem::path RecordingWriter::BuildOutputPath(const std::filesystem::path& directory) {
  SYSTEMTIME time{};
  GetLocalTime(&time);
  wchar_t filename[128]{};
  swprintf_s(filename, L"recording_%04u%02u%02u_%02u%02u%02u_%03u.mp4", time.wYear, time.wMonth,
             time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
  return directory / filename;
}

}  // namespace klip
