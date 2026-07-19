#include "klip/encoding/audio_encoder.h"

#include <algorithm>
#include <memory>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

namespace klip {
namespace {

struct FrameDeleter {
  void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};

std::int64_t FramesTo100ns(int frames) {
  return static_cast<std::int64_t>(frames) * 10'000'000LL / AudioEncoder::kSampleRate;
}

}  // namespace

AudioEncoder::AudioEncoder(PacketRouter& router, Logger& logger) : router_(router), logger_(logger) {}

AudioEncoder::~AudioEncoder() noexcept { Shutdown(); }

bool AudioEncoder::Initialize(std::int64_t bitrate, Error& error) {
  Shutdown();
  std::scoped_lock lock(mutex_);
  const auto* encoder = avcodec_find_encoder_by_name("aac");
  if (encoder == nullptr) encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (encoder == nullptr) {
    error = Error{ErrorComponent::kAudioEncoder, "discover AAC encoder",
                  "FFmpeg does not provide an AAC encoder"};
    return false;
  }
  codec_ = avcodec_alloc_context3(encoder);
  if (codec_ == nullptr) {
    error = Error{ErrorComponent::kAudioEncoder, "allocate AAC context",
                  "avcodec_alloc_context3 returned null"};
    return false;
  }
  codec_->sample_rate = kSampleRate;
  codec_->sample_fmt = AV_SAMPLE_FMT_FLTP;
  codec_->time_base = AVRational{1, 10'000'000};
  codec_->pkt_timebase = codec_->time_base;
  codec_->bit_rate = bitrate;
  av_channel_layout_default(&codec_->ch_layout, kChannels);
  const auto result = avcodec_open2(codec_, encoder, nullptr);
  if (result < 0) {
    error = MakeFfmpegError(ErrorComponent::kAudioEncoder, "open AAC encoder", result);
    avcodec_free_context(&codec_);
    return false;
  }
  logger_.Info("AAC encoder initialized");
  return true;
}

void AudioEncoder::Shutdown() noexcept {
  std::scoped_lock lock(mutex_);
  FlushLocked();
  if (codec_ != nullptr) avcodec_free_context(&codec_);
  last_pts_ = AV_NOPTS_VALUE;
  last_dts_ = AV_NOPTS_VALUE;
}

void AudioEncoder::Flush() noexcept {
  std::scoped_lock lock(mutex_);
  FlushLocked();
}

void AudioEncoder::FlushLocked() noexcept {
  if (codec_ != nullptr) {
    avcodec_send_frame(codec_, nullptr);
    Drain(last_pts_ == AV_NOPTS_VALUE ? 0 : last_pts_);
  }
}

bool AudioEncoder::Encode(const std::vector<float>& interleaved, int frames, std::int64_t pts_100ns,
                          Error& error) {
  std::scoped_lock lock(mutex_);
  if (codec_ == nullptr || frames <= 0 ||
      interleaved.size() < static_cast<std::size_t>(frames * kChannels)) {
    return false;
  }
  std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
  if (!frame) {
    error = Error{ErrorComponent::kAudioEncoder, "allocate audio frame",
                  "av_frame_alloc returned null"};
    return false;
  }
  frame->format = codec_->sample_fmt;
  frame->nb_samples = frames;
  frame->sample_rate = codec_->sample_rate;
  av_channel_layout_copy(&frame->ch_layout, &codec_->ch_layout);
  frame->pts = pts_100ns;
  auto result = av_frame_get_buffer(frame.get(), 0);
  if (result < 0) {
    error = MakeFfmpegError(ErrorComponent::kAudioEncoder, "allocate audio samples", result);
    return false;
  }
  auto* left = reinterpret_cast<float*>(frame->data[0]);
  auto* right = reinterpret_cast<float*>(frame->data[1]);
  for (int index = 0; index < frames; ++index) {
    left[index] = interleaved[static_cast<std::size_t>(index) * 2];
    right[index] = interleaved[static_cast<std::size_t>(index) * 2 + 1];
  }
  result = avcodec_send_frame(codec_, frame.get());
  if (result < 0) {
    error = MakeFfmpegError(ErrorComponent::kAudioEncoder, "submit audio frame", result);
    return false;
  }
  Drain(pts_100ns);
  return true;
}

int AudioEncoder::FrameSize() const {
  std::scoped_lock lock(mutex_);
  return codec_ != nullptr && codec_->frame_size > 0 ? codec_->frame_size : 1024;
}

bool AudioEncoder::SnapshotCodec(CodecSnapshot& snapshot) const {
  std::scoped_lock lock(mutex_);
  if (codec_ == nullptr) return false;
  CodecParametersPtr parameters(avcodec_parameters_alloc());
  if (!parameters || avcodec_parameters_from_context(parameters.get(), codec_) < 0) {
    return false;
  }
  snapshot.parameters = std::move(parameters);
  snapshot.time_base = codec_->time_base;
  return true;
}

void AudioEncoder::Drain(std::int64_t fallback_pts) noexcept {
  PacketPtr packet(av_packet_alloc());
  if (!packet || codec_ == nullptr) return;
  for (;;) {
    const auto result = avcodec_receive_packet(codec_, packet.get());
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
    if (result < 0) {
      logger_.ErrorMessage(
          MakeFfmpegError(ErrorComponent::kAudioEncoder, "receive AAC packet", result));
      return;
    }
    Normalize(packet.get(), fallback_pts);
    router_.Publish(packet.get(), StreamKind::kAudio, codec_->time_base);
    av_packet_unref(packet.get());
  }
}

void AudioEncoder::Normalize(AVPacket* packet, std::int64_t fallback_pts) noexcept {
  const auto frame_size = codec_ != nullptr && codec_->frame_size > 0 ? codec_->frame_size : 1024;
  const auto duration = FramesTo100ns(frame_size);
  auto pts = packet->pts == AV_NOPTS_VALUE ? fallback_pts : packet->pts;
  if (last_pts_ != AV_NOPTS_VALUE && pts <= last_pts_) pts = last_pts_ + duration;
  auto dts = packet->dts == AV_NOPTS_VALUE ? pts : packet->dts;
  if (last_dts_ != AV_NOPTS_VALUE && dts <= last_dts_) dts = last_dts_ + duration;
  pts = std::max(pts, dts);
  packet->pts = pts;
  packet->dts = dts;
  if (packet->duration <= 0) packet->duration = duration;
  last_pts_ = pts;
  last_dts_ = dts;
}

}  // namespace klip
