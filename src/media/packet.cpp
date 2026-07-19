#include "klip/media/packet.h"

extern "C" {
#include <libavutil/mathematics.h>
}

namespace klip {
namespace {

constexpr AVRational kHundredNanoseconds{1, 10'000'000};

std::int64_t To100ns(std::int64_t value, AVRational time_base) {
  return value == AV_NOPTS_VALUE ? 0 : av_rescale_q(value, time_base, kHundredNanoseconds);
}

}  // namespace

PacketDescriptor DescribePacket(const EncodedPacket& encoded) {
  if (!encoded.packet) {
    return {};
  }
  const auto* packet = encoded.packet.get();
  return PacketDescriptor{
      encoded.kind,
      To100ns(packet->pts, encoded.time_base),
      To100ns(packet->dts == AV_NOPTS_VALUE ? packet->pts : packet->dts, encoded.time_base),
      To100ns(packet->duration, encoded.time_base),
      packet->size > 0 ? static_cast<std::size_t>(packet->size) : 0,
      (packet->flags & AV_PKT_FLAG_KEY) != 0};
}

}  // namespace klip
