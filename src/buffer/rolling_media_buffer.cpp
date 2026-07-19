#include "klip/buffer/rolling_media_buffer.h"

#include <algorithm>

#include "klip/core/clip_selection.h"

namespace klip {

RollingMediaBuffer::RollingMediaBuffer(double maximum_seconds, std::size_t maximum_bytes)
    : maximum_seconds_(maximum_seconds), maximum_bytes_(maximum_bytes) {}

bool RollingMediaBuffer::Push(const AVPacket* packet, StreamKind kind, AVRational time_base) {
  if (packet == nullptr) {
    return false;
  }
  PacketPtr clone(av_packet_clone(packet));
  if (!clone) {
    return false;
  }

  const auto packet_bytes = clone->size > 0 ? static_cast<std::size_t>(clone->size) : 0;
  std::scoped_lock lock(mutex_);
  packets_.push_back(EncodedPacket{std::move(clone), kind, time_base});
  bytes_ += packet_bytes;
  EvictLocked();
  return true;
}

std::vector<EncodedPacket> RollingMediaBuffer::Snapshot(double seconds) const {
  std::vector<EncodedPacket> result;
  std::vector<PacketDescriptor> descriptors;
  {
    std::scoped_lock lock(mutex_);
    descriptors.reserve(packets_.size());
    for (const auto& packet : packets_) {
      descriptors.push_back(DescribePacket(packet));
    }
    const auto range = SelectClipRange(descriptors, seconds);
    if (range.Empty()) {
      return {};
    }
    std::int64_t clip_start = descriptors[range.begin].pts_100ns;
    for (std::size_t index = range.begin; index < range.end; ++index) {
      if (descriptors[index].kind == StreamKind::kVideo && descriptors[index].keyframe) {
        clip_start = descriptors[index].pts_100ns;
        break;
      }
    }
    result.reserve(range.end - range.begin);
    for (std::size_t index = range.begin; index < range.end; ++index) {
      if (descriptors[index].pts_100ns < clip_start) {
        continue;
      }
      const auto& packet = packets_[index];
      PacketPtr clone(packet.packet ? av_packet_clone(packet.packet.get()) : nullptr);
      if (clone) {
        result.push_back(EncodedPacket{std::move(clone), packet.kind, packet.time_base});
      }
    }
  }
  std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    return DescribePacket(left).dts_100ns < DescribePacket(right).dts_100ns;
  });
  return result;
}

RollingBufferStats RollingMediaBuffer::Stats() const {
  std::scoped_lock lock(mutex_);
  RollingBufferStats stats{packets_.size(), bytes_, 0.0};
  std::int64_t first = 0;
  std::int64_t last = 0;
  bool found = false;
  for (const auto& packet : packets_) {
    if (packet.kind != StreamKind::kVideo || !packet.packet) {
      continue;
    }
    const auto pts = DescribePacket(packet).pts_100ns;
    if (!found) {
      first = pts;
      found = true;
    }
    last = pts;
  }
  if (found && last >= first) {
    stats.duration_seconds = static_cast<double>(last - first) / 10'000'000.0;
  }
  return stats;
}

void RollingMediaBuffer::Clear() {
  std::scoped_lock lock(mutex_);
  packets_.clear();
  bytes_ = 0;
}

void RollingMediaBuffer::EvictLocked() {
  while (!packets_.empty()) {
    const auto first = DescribePacket(packets_.front()).pts_100ns;
    const auto last = DescribePacket(packets_.back()).pts_100ns;
    if (!ExceedsRollingLimits(first, last, bytes_, maximum_seconds_, maximum_bytes_)) {
      break;
    }
    const auto size = packets_.front().packet && packets_.front().packet->size > 0
                          ? static_cast<std::size_t>(packets_.front().packet->size)
                          : 0;
    bytes_ -= std::min(bytes_, size);
    packets_.pop_front();
  }
}

}  // namespace klip
