#pragma once

#include <atomic>
#include <mutex>

#include "klip/buffer/rolling_media_buffer.h"
#include "klip/core/application_state.h"
#include "klip/core/logger.h"

namespace klip {

class RecordingWriter;

class PacketRouter {
 public:
  PacketRouter(RollingMediaBuffer& buffer, RecordingWriter& recording, ApplicationState& state,
               Logger& logger)
      : buffer_(buffer), recording_(recording), state_(state), logger_(logger) {}

  void Publish(const AVPacket* packet, StreamKind kind, AVRational time_base) noexcept;
  void RequestRecordingStop(std::uint64_t video_frame_target,
                            std::uint64_t audio_frame_target) noexcept;
  void ResetTimeline(bool stop_recording = true) noexcept;

 private:
  RollingMediaBuffer& buffer_;
  RecordingWriter& recording_;
  ApplicationState& state_;
  Logger& logger_;
  std::mutex replay_mutex_;
  bool replay_recovery_required_ = false;
  std::atomic<bool> replay_failure_reported_{false};
  std::atomic<std::uint64_t> routed_video_packets_{0};
  std::atomic<std::uint64_t> routed_audio_packets_{0};
};

}  // namespace klip
