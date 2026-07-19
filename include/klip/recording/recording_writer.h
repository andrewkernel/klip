#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#include "klip/core/application_state.h"
#include "klip/core/bounded_queue.h"
#include "klip/core/config.h"
#include "klip/core/logger.h"
#include "klip/media/packet.h"

namespace klip {

class RecordingWriter {
 public:
  using SnapshotProvider = std::function<bool(CodecSnapshot&)>;

  RecordingWriter(ApplicationState& state, Logger& logger);
  ~RecordingWriter() noexcept;

  RecordingWriter(const RecordingWriter&) = delete;
  RecordingWriter& operator=(const RecordingWriter&) = delete;

  bool Initialize(AppConfig config, SnapshotProvider video, SnapshotProvider audio, Error& error);
  void Shutdown() noexcept;
  bool StartRecording(Error& error);
  void StopRecording() noexcept;
  void Publish(const AVPacket* packet, StreamKind kind, AVRational time_base) noexcept;
  void Tick();

  [[nodiscard]] bool IsRecording() const noexcept {
    return recording_.load(std::memory_order_acquire);
  }

 private:
  using PacketQueue = BlockingBoundedQueue<EncodedPacket>;

  void Worker(std::shared_ptr<PacketQueue> queue, std::filesystem::path output_path,
              CodecSnapshot video, CodecSnapshot audio, bool has_audio) noexcept;
  static std::filesystem::path BuildOutputPath(const std::filesystem::path& directory);

  ApplicationState& state_;
  Logger& logger_;
  AppConfig config_;
  SnapshotProvider video_snapshot_;
  SnapshotProvider audio_snapshot_;
  mutable std::mutex control_mutex_;
  std::shared_ptr<PacketQueue> active_queue_;
  std::jthread worker_;
  std::filesystem::path active_path_;
  std::chrono::steady_clock::time_point started_at_{};
  std::atomic<bool> initialized_{false};
  std::atomic<bool> recording_{false};
  std::atomic<bool> finalizing_{false};
  std::atomic<std::uint64_t> dropped_packets_{0};
};

}  // namespace klip
