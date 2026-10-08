#include "klip/buffer/rolling_media_buffer.h"

#include <algorithm>
#include <exception>

#include "klip/core/clip_selection.h"

namespace klip {

RollingMediaBuffer::RollingMediaBuffer(double maximum_seconds, std::size_t maximum_bytes)
    : maximum_seconds_(maximum_seconds), maximum_bytes_(maximum_bytes) {}

bool RollingMediaBuffer::Push(const AVPacket* packet, StreamKind kind, AVRational time_base,
                              Error& error) {
  if (packet == nullptr) {
    error = Error{ErrorComponent::kRollingBuffer, "retain encoded packet",
                  "encoder returned a null packet"};
    return false;
  }
  PacketPtr clone(av_packet_clone(packet));
  if (!clone) {
    error = Error{ErrorComponent::kRollingBuffer, "clone encoded packet",
                  "av_packet_clone failed; replay history was reset"};
    return false;
  }

  const auto packet_bytes = clone->size > 0 ? static_cast<std::size_t>(clone->size) : 0;
  EncodedPacket encoded{std::move(clone), kind, time_base};
  const auto descriptor = DescribePacket(encoded);
  std::scoped_lock lock(mutex_);
  try {
    packets_.push_back(std::move(encoded));
    bytes_ += packet_bytes;
    if (kind == StreamKind::kVideo) {
      if (!has_video_) first_video_pts_100ns_ = descriptor.pts_100ns;
      last_video_pts_100ns_ = descriptor.pts_100ns;
      has_video_ = true;
    }
    EvictLocked();
  } catch (const std::exception& exception) {
    error = Error{ErrorComponent::kRollingBuffer, "retain encoded packet",
                  std::string("could not retain packet: ") + exception.what()};
    return false;
  }
  return true;
}

bool RollingMediaBuffer::Snapshot(double seconds, std::vector<EncodedPacket>& result,
                                  Error& error) const {
  result.clear();
  try {
    std::vector<PacketDescriptor> descriptors;
    std::scoped_lock lock(mutex_);
    descriptors.reserve(packets_.size());
    for (const auto& packet : packets_) {
      descriptors.push_back(DescribePacket(packet));
    }
    const auto range = SelectClipRange(descriptors, seconds);
    if (range.Empty()) {
      return true;
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
      // With B-frames, packets after the opening keyframe in decode order can legitimately carry
      // an earlier presentation timestamp. Dropping those video packets creates holes in the
      // replay cadence; only trim pre-roll audio against the keyframe's presentation boundary.
      if (descriptors[index].kind == StreamKind::kAudio &&
          descriptors[index].pts_100ns < clip_start) {
        continue;
      }
      const auto& packet = packets_[index];
      PacketPtr clone(packet.packet ? av_packet_clone(packet.packet.get()) : nullptr);
      if (!clone) {
        result.clear();
        error = Error{ErrorComponent::kRollingBuffer, "snapshot replay packets",
                      "packet cloning failed; refusing to save an incomplete clip"};
        return false;
      }
      result.push_back(EncodedPacket{std::move(clone), packet.kind, packet.time_base});
    }
  } catch (const std::exception& exception) {
    result.clear();
    error = Error{ErrorComponent::kRollingBuffer, "snapshot replay packets",
                  std::string("could not create a complete replay snapshot: ") + exception.what()};
    return false;
  }
  try {
    std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
      return DescribePacket(left).dts_100ns < DescribePacket(right).dts_100ns;
    });
  } catch (const std::exception& exception) {
    result.clear();
    error = Error{ErrorComponent::kRollingBuffer, "sort replay snapshot",
                  std::string("could not order a complete replay snapshot: ") + exception.what()};
    return false;
  }
  return true;
}

RollingBufferStats RollingMediaBuffer::Stats() const {
  std::scoped_lock lock(mutex_);
  RollingBufferStats stats{packets_.size(), bytes_, 0.0};
  if (has_video_ && last_video_pts_100ns_ >= first_video_pts_100ns_) {
    stats.duration_seconds =
        static_cast<double>(last_video_pts_100ns_ - first_video_pts_100ns_) / 10'000'000.0;
  }
  return stats;
}

void RollingMediaBuffer::Reconfigure(double maximum_seconds, std::size_t maximum_bytes) {
  std::scoped_lock lock(mutex_);
  maximum_seconds_ = maximum_seconds;
  maximum_bytes_ = maximum_bytes;
  EvictLocked();
}

void RollingMediaBuffer::Clear() {
  std::scoped_lock lock(mutex_);
  packets_.clear();
  bytes_ = 0;
  first_video_pts_100ns_ = 0;
  last_video_pts_100ns_ = 0;
  has_video_ = false;
}

void RollingMediaBuffer::EvictLocked() {
  bool removed_video = false;
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
    removed_video = removed_video || packets_.front().kind == StreamKind::kVideo;
    packets_.pop_front();
  }
  if (removed_video) RefreshVideoBoundsLocked();
}

void RollingMediaBuffer::RefreshVideoBoundsLocked() noexcept {
  has_video_ = false;
  for (const auto& packet : packets_) {
    if (packet.kind != StreamKind::kVideo || !packet.packet) continue;
    first_video_pts_100ns_ = DescribePacket(packet).pts_100ns;
    has_video_ = true;
    break;
  }
  if (!has_video_) {
    first_video_pts_100ns_ = 0;
    last_video_pts_100ns_ = 0;
  }
}

}  // namespace klip
