#include "klip/encoding/video_encoder.h"

#include <algorithm>
#include <memory>

#include "klip/core/encoder_selection.h"

extern "C" {
#include <libavutil/hwcontext_d3d11va.h>
}

namespace klip {
namespace {

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.starts_with(prefix);
}

struct FrameDeleter {
  void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};

}  // namespace

struct VideoEncoder::RecycleCookie {
  TextureRecycler recycler;
  ID3D11Texture2D* texture = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

VideoEncoder::VideoEncoder(PacketRouter& router, ApplicationState& state, Logger& logger)
    : router_(router), state_(state), logger_(logger) {}

VideoEncoder::~VideoEncoder() noexcept { Shutdown(); }

bool VideoEncoder::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                              std::uint32_t adapter_vendor_id, const AppConfig& config,
                              Error& error) {
  Shutdown();
  if (device == nullptr || context == nullptr) {
    error =
        Error{ErrorComponent::kVideoEncoder, "initialize", "D3D11 device and context are required"};
    return false;
  }
  device_.copy_from(device);
  context_.copy_from(context);
  adapter_vendor_id_ = adapter_vendor_id;
  config_ = config;
  return CreateHardwareDevice(error);
}

void VideoEncoder::Shutdown() noexcept {
  std::scoped_lock lock(mutex_);
  FlushLocked();
  ReleaseCodec();
  router_.ResetTimeline();
  if (hardware_device_ != nullptr) {
    av_buffer_unref(&hardware_device_);
  }
  device_ = nullptr;
  context_ = nullptr;
}

void VideoEncoder::Flush() noexcept {
  std::scoped_lock lock(mutex_);
  FlushLocked();
}

void VideoEncoder::RestartTimeline() noexcept {
  std::scoped_lock lock(mutex_);
  FlushLocked();
  router_.ResetTimeline();
  ReleaseCodec();
}

bool VideoEncoder::Encode(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height,
                          std::int64_t pts_100ns, TextureRecycler recycler, Error& error) {
  std::scoped_lock lock(mutex_);
  if (!EnsureOpen(width, height, error) || texture == nullptr) {
    return false;
  }

  std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
  if (!frame) {
    error = Error{ErrorComponent::kVideoEncoder, "allocate frame", "av_frame_alloc returned null"};
    return false;
  }
  frame->format = AV_PIX_FMT_D3D11;
  frame->width = static_cast<int>(width);
  frame->height = static_cast<int>(height);
  frame->pts = pts_100ns;
  if (force_keyframe_) frame->pict_type = AV_PICTURE_TYPE_I;
  if (hardware_frames_ != nullptr) {
    frame->hw_frames_ctx = av_buffer_ref(hardware_frames_);
  }

  texture->AddRef();
  auto* cookie = new RecycleCookie{std::move(recycler), texture, width, height};
  frame->data[0] = reinterpret_cast<std::uint8_t*>(texture);
  frame->data[1] = nullptr;
  frame->buf[0] = av_buffer_create(reinterpret_cast<std::uint8_t*>(texture),
                                   sizeof(ID3D11Texture2D*), ReleaseTexture, cookie, 0);
  if (frame->buf[0] == nullptr) {
    texture->Release();
    delete cookie;
    error = Error{ErrorComponent::kVideoEncoder, "wrap D3D11 texture",
                  "av_buffer_create returned null"};
    return false;
  }

  auto result = avcodec_send_frame(codec_, frame.get());
  if (result == AVERROR(EAGAIN)) {
    Drain(pts_100ns);
    result = avcodec_send_frame(codec_, frame.get());
  }
  if (result < 0) {
    error = MakeFfmpegError(ErrorComponent::kVideoEncoder, "submit D3D11 frame", result,
                            state_.Snapshot().selected_encoder);
    return false;
  }
  force_keyframe_ = false;
  Drain(pts_100ns);
  return true;
}

bool VideoEncoder::SnapshotCodec(CodecSnapshot& snapshot) const {
  std::scoped_lock lock(mutex_);
  if (codec_ == nullptr) {
    return false;
  }
  CodecParametersPtr parameters(avcodec_parameters_alloc());
  if (!parameters || avcodec_parameters_from_context(parameters.get(), codec_) < 0) {
    return false;
  }
  snapshot.parameters = std::move(parameters);
  snapshot.time_base = codec_->time_base;
  return true;
}

void VideoEncoder::ReleaseTexture(void* opaque, std::uint8_t*) noexcept {
  std::unique_ptr<RecycleCookie> cookie(static_cast<RecycleCookie*>(opaque));
  if (!cookie || cookie->texture == nullptr) {
    return;
  }
  if (cookie->recycler) {
    cookie->recycler(cookie->texture, cookie->width, cookie->height);
  } else {
    cookie->texture->Release();
  }
}

bool VideoEncoder::CreateHardwareDevice(Error& error) {
  hardware_device_ = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
  if (hardware_device_ == nullptr) {
    error = Error{ErrorComponent::kVideoEncoder, "allocate hardware device",
                  "av_hwdevice_ctx_alloc returned null"};
    return false;
  }
  auto* base = reinterpret_cast<AVHWDeviceContext*>(hardware_device_->data);
  auto* d3d = reinterpret_cast<AVD3D11VADeviceContext*>(base->hwctx);
  d3d->device = device_.get();
  d3d->device->AddRef();
  d3d->device_context = context_.get();
  d3d->device_context->AddRef();
  const auto result = av_hwdevice_ctx_init(hardware_device_);
  if (result < 0) {
    error =
        MakeFfmpegError(ErrorComponent::kVideoEncoder, "initialize D3D11 hardware device", result);
    return false;
  }
  return true;
}

bool VideoEncoder::CreateFramesContext(std::uint32_t width, std::uint32_t height, Error& error) {
  if (hardware_frames_ != nullptr) {
    av_buffer_unref(&hardware_frames_);
  }
  hardware_frames_ = av_hwframe_ctx_alloc(hardware_device_);
  if (hardware_frames_ == nullptr) {
    error = Error{ErrorComponent::kVideoEncoder, "allocate frame context",
                  "av_hwframe_ctx_alloc returned null"};
    return false;
  }
  auto* frames = reinterpret_cast<AVHWFramesContext*>(hardware_frames_->data);
  frames->format = AV_PIX_FMT_D3D11;
  frames->sw_format = AV_PIX_FMT_NV12;
  frames->width = static_cast<int>(width);
  frames->height = static_cast<int>(height);
  frames->initial_pool_size = 0;
  auto* d3d = reinterpret_cast<AVD3D11VAFramesContext*>(frames->hwctx);
  d3d->BindFlags = D3D11_BIND_SHADER_RESOURCE;
  const auto result = av_hwframe_ctx_init(hardware_frames_);
  if (result < 0) {
    error =
        MakeFfmpegError(ErrorComponent::kVideoEncoder, "initialize D3D11 frame context", result);
    av_buffer_unref(&hardware_frames_);
    return false;
  }
  return true;
}

bool VideoEncoder::EnsureOpen(std::uint32_t width, std::uint32_t height, Error& error) {
  if (codec_ != nullptr && width == width_ && height == height_) {
    return true;
  }
  const bool replacing_codec = codec_ != nullptr;
  FlushLocked();
  if (replacing_codec) router_.ResetTimeline();
  ReleaseCodec();
  Error frames_error;
  if (!CreateFramesContext(width, height, frames_error)) {
    logger_.Warning(frames_error.ToString() + "; trying direct D3D11 texture input");
  }

  for (const auto& candidate : BuildPreference()) {
    Error candidate_error;
    if (TryOpen(candidate, width, height, candidate_error)) {
      state_.SetEncoder(candidate);
      state_.ClearError();
      logger_.Info("Selected video encoder: " + candidate);
      return true;
    }
    logger_.Warning(candidate_error.ToString());
  }
  error = Error{ErrorComponent::kVideoEncoder, "select encoder",
                "no configured hardware H.264 encoder accepted D3D11 input"};
  return false;
}

bool VideoEncoder::TryOpen(const std::string& name, std::uint32_t width, std::uint32_t height,
                           Error& error) {
  const auto* encoder = avcodec_find_encoder_by_name(name.c_str());
  if (encoder == nullptr) {
    error = Error{ErrorComponent::kVideoEncoder,
                  "discover encoder",
                  "encoder is not present in this FFmpeg build",
                  {},
                  {},
                  name};
    return false;
  }
  auto* context = avcodec_alloc_context3(encoder);
  if (context == nullptr) {
    error = Error{ErrorComponent::kVideoEncoder,
                  "allocate encoder context",
                  "avcodec_alloc_context3 returned null",
                  {},
                  {},
                  name};
    return false;
  }
  context->width = static_cast<int>(width);
  context->height = static_cast<int>(height);
  context->pix_fmt = AV_PIX_FMT_D3D11;
  context->sw_pix_fmt = AV_PIX_FMT_NV12;
  context->time_base = AVRational{1, 10'000'000};
  context->pkt_timebase = context->time_base;
  context->framerate = AVRational{static_cast<int>(config_.target_fps), 1};
  context->gop_size = static_cast<int>(config_.target_fps);
  context->max_b_frames = 0;
  context->thread_count = 1;
  context->flags |= AV_CODEC_FLAG_LOW_DELAY;
  context->bit_rate = config_.video_bitrate;
  context->hw_device_ctx = av_buffer_ref(hardware_device_);
  if (hardware_frames_ != nullptr) {
    context->hw_frames_ctx = av_buffer_ref(hardware_frames_);
  }
  AVDictionary* options = nullptr;
  SetOptions(&options, name, config_.target_fps, config_.encoder_quality);
  const auto result = avcodec_open2(context, encoder, &options);
  av_dict_free(&options);
  if (result < 0) {
    error = MakeFfmpegError(ErrorComponent::kVideoEncoder, "open encoder", result, name);
    avcodec_free_context(&context);
    return false;
  }
  codec_ = context;
  width_ = width;
  height_ = height;
  last_pts_ = AV_NOPTS_VALUE;
  last_dts_ = AV_NOPTS_VALUE;
  force_keyframe_ = true;
  logged_first_packet_ = false;
  logged_first_keyframe_ = false;
  return true;
}

void VideoEncoder::Drain(std::int64_t fallback_pts) {
  PacketPtr packet(av_packet_alloc());
  if (!packet || codec_ == nullptr) {
    return;
  }
  for (;;) {
    const auto result = avcodec_receive_packet(codec_, packet.get());
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
      return;
    }
    if (result < 0) {
      logger_.ErrorMessage(
          MakeFfmpegError(ErrorComponent::kVideoEncoder, "receive encoded packet", result));
      return;
    }
    if (!logged_first_packet_) {
      logger_.Info("First encoded video packet ready");
      logged_first_packet_ = true;
    }
    if (!logged_first_keyframe_ && (packet->flags & AV_PKT_FLAG_KEY) != 0) {
      logger_.Info("First video keyframe ready");
      logged_first_keyframe_ = true;
    }
    NormalizeTimestamps(packet.get(), fallback_pts);
    router_.Publish(packet.get(), StreamKind::kVideo, codec_->time_base);
    av_packet_unref(packet.get());
  }
}

void VideoEncoder::NormalizeTimestamps(AVPacket* packet, std::int64_t fallback_pts) {
  const auto duration =
      std::max<std::int64_t>(1, 10'000'000 / static_cast<std::int64_t>(config_.target_fps));
  auto pts = packet->pts == AV_NOPTS_VALUE ? fallback_pts : packet->pts;
  if (last_pts_ != AV_NOPTS_VALUE && pts <= last_pts_) {
    pts = last_pts_ + duration;
  }
  auto dts = packet->dts == AV_NOPTS_VALUE ? pts : packet->dts;
  if (last_dts_ != AV_NOPTS_VALUE && dts <= last_dts_) {
    dts = last_dts_ + duration;
  }
  pts = std::max(pts, dts);
  packet->pts = pts;
  packet->dts = dts;
  if (packet->duration <= 0) {
    packet->duration = duration;
  }
  last_pts_ = pts;
  last_dts_ = dts;
}

void VideoEncoder::FlushLocked() noexcept {
  if (codec_ != nullptr) {
    avcodec_send_frame(codec_, nullptr);
    Drain(last_pts_ == AV_NOPTS_VALUE ? 0 : last_pts_);
  }
}

void VideoEncoder::ReleaseCodec() noexcept {
  if (codec_ != nullptr) {
    avcodec_free_context(&codec_);
  }
  if (hardware_frames_ != nullptr) {
    av_buffer_unref(&hardware_frames_);
  }
  width_ = 0;
  height_ = 0;
  state_.SetEncoder({});
}

std::vector<std::string> VideoEncoder::BuildPreference() const {
  return PrioritizeEncoders(config_.encoder_preferences, adapter_vendor_id_);
}

void VideoEncoder::SetOptions(AVDictionary** options, const std::string& name, std::uint32_t fps,
                              EncoderQuality quality) {
  av_dict_set(options, "g", std::to_string(fps).c_str(), 0);
  av_dict_set(options, "bf", "0", 0);
  if (StartsWith(name, "h264_nvenc")) {
    const char* preset = quality == EncoderQuality::kPerformance ? "p3"
                         : quality == EncoderQuality::kQuality   ? "p6"
                                                                : "p5";
    av_dict_set(options, "preset", preset, 0);
    av_dict_set(options, "tune", "ll", 0);
    av_dict_set(options, "rc", "cbr", 0);
    av_dict_set(options, "rc-lookahead", "0", 0);
    av_dict_set(options, "delay", "0", 0);
    av_dict_set(options, "surfaces", "4", 0);
    av_dict_set(options, "zerolatency", "1", 0);
  } else if (StartsWith(name, "h264_amf")) {
    av_dict_set(options, "usage", "ultralowlatency", 0);
    const char* amf_quality = quality == EncoderQuality::kPerformance ? "speed"
                              : quality == EncoderQuality::kQuality   ? "quality"
                                                                     : "balanced";
    av_dict_set(options, "quality", amf_quality, 0);
    av_dict_set(options, "rc", "cbr", 0);
    av_dict_set(options, "async_depth", "2", 0);
    av_dict_set(options, "latency", "1", 0);
    av_dict_set(options, "preanalysis", "0", 0);
  } else if (StartsWith(name, "h264_mf")) {
    av_dict_set(options, "hw_encoding", "1", 0);
    av_dict_set(options, "rate_control", "cbr", 0);
    const char* scenario = quality == EncoderQuality::kPerformance ? "display_remoting"
                           : quality == EncoderQuality::kQuality   ? "archive"
                                                                  : "live_streaming";
    av_dict_set(options, "scenario", scenario, 0);
  }
}

}  // namespace klip
