#pragma once

#include "klip/buffer/rolling_media_buffer.h"

namespace klip {

class RecordingWriter;

class PacketRouter {
 public:
  PacketRouter(RollingMediaBuffer& buffer, RecordingWriter& recording)
      : buffer_(buffer), recording_(recording) {}

  void Publish(const AVPacket* packet, StreamKind kind, AVRational time_base) noexcept;
  void ResetTimeline() noexcept;

 private:
  RollingMediaBuffer& buffer_;
  RecordingWriter& recording_;
};

}  // namespace klip
