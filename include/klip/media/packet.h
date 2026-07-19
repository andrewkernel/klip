#pragma once

#include <cstdint>
#include <memory>

#include "klip/core/clip_selection.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace klip {

struct PacketDeleter {
  void operator()(AVPacket* packet) const noexcept {
    if (packet != nullptr) {
      av_packet_free(&packet);
    }
  }
};

using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

struct EncodedPacket {
  PacketPtr packet;
  StreamKind kind = StreamKind::kVideo;
  AVRational time_base{1, 10'000'000};
};

struct CodecParametersDeleter {
  void operator()(AVCodecParameters* parameters) const noexcept {
    if (parameters != nullptr) {
      avcodec_parameters_free(&parameters);
    }
  }
};

using CodecParametersPtr = std::unique_ptr<AVCodecParameters, CodecParametersDeleter>;

struct CodecSnapshot {
  CodecParametersPtr parameters;
  AVRational time_base{1, 10'000'000};
};

PacketDescriptor DescribePacket(const EncodedPacket& packet);

}  // namespace klip
