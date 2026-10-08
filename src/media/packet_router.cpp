#include "klip/media/packet_router.h"

#include "klip/recording/recording_writer.h"

namespace klip {

void PacketRouter::Publish(const AVPacket* packet, StreamKind kind, AVRational time_base) noexcept {
  if (packet == nullptr) return;
  {
    std::scoped_lock lock(replay_mutex_);
    const bool keyframe = kind == StreamKind::kVideo && (packet->flags & AV_PKT_FLAG_KEY) != 0;
    if (replay_recovery_required_ && !keyframe) {
      // Preserve recording independently, but never retain dependent pictures
      // without the reference frame chain that precedes them.
    } else {
      if (replay_recovery_required_) {
        buffer_.Clear();
        replay_recovery_required_ = false;
        if (replay_failure_reported_.exchange(false, std::memory_order_acq_rel)) {
          state_.ClearError(ErrorComponent::kRollingBuffer);
          logger_.Info("Replay buffer recovered at video keyframe");
        }
      }
      Error error;
      if (!buffer_.Push(packet, kind, time_base, error)) {
        // A missing compressed packet can invalidate dependent pictures. Drop the
        // replay history, then wait for a fresh keyframe before accepting packets.
        buffer_.Clear();
        replay_recovery_required_ = true;
        state_.SetError(error);
        if (!replay_failure_reported_.exchange(true, std::memory_order_acq_rel))
          logger_.ErrorMessage(error);
      }
    }
  }
  auto video_packets = routed_video_packets_.load(std::memory_order_relaxed);
  auto audio_packets = routed_audio_packets_.load(std::memory_order_relaxed);
  if (kind == StreamKind::kVideo)
    video_packets = routed_video_packets_.fetch_add(1, std::memory_order_release) + 1;
  else
    audio_packets = routed_audio_packets_.fetch_add(1, std::memory_order_release) + 1;
  recording_.Publish(packet, kind, time_base, video_packets, audio_packets);
}

void PacketRouter::RequestRecordingStop(std::uint64_t video_frame_target,
                                        std::uint64_t audio_frame_target) noexcept {
  recording_.RequestStopRecording(video_frame_target, audio_frame_target,
                                  routed_video_packets_.load(std::memory_order_acquire),
                                  routed_audio_packets_.load(std::memory_order_acquire));
}

void PacketRouter::ResetTimeline(bool stop_recording) noexcept {
  if (stop_recording) recording_.StopRecording();
  {
    std::scoped_lock lock(replay_mutex_);
    buffer_.Clear();
    replay_recovery_required_ = false;
  }
  replay_failure_reported_.store(false, std::memory_order_release);
}

}  // namespace klip
