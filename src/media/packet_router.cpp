#include "klip/media/packet_router.h"

#include "klip/recording/recording_writer.h"

namespace klip {

void PacketRouter::Publish(const AVPacket* packet, StreamKind kind,
                           AVRational time_base) noexcept {
  if (packet == nullptr) return;
  buffer_.Push(packet, kind, time_base);
  recording_.Publish(packet, kind, time_base);
}

void PacketRouter::ResetTimeline() noexcept {
  recording_.StopRecording();
  buffer_.Clear();
}

}  // namespace klip
