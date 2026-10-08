#include "klip/encoding/video_encoder.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

#include "klip/core/encoder_selection.h"
#include "klip/core/encoder_tuning.h"

extern "C" {
#include <libavutil/hwcontext_d3d11va.h>
}

namespace klip {
namespace {

struct FrameDeleter {
  void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};

class TextureReturnGuard {
 public:
  TextureReturnGuard(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height,
                     VideoEncoder::TextureRecycler recycler) noexcept
      : texture_(texture), width_(width), height_(height), recycler_(std::move(recycler)) {}
  ~TextureReturnGuard() noexcept { Return(true); }

  TextureReturnGuard(const TextureReturnGuard&) = delete;
  TextureReturnGuard& operator=(const TextureReturnGuard&) = delete;

  void Return(bool reusable) noexcept {
    if (!armed_) return;
    armed_ = false;
    if (texture_ == nullptr || !recycler_) return;
    texture_->AddRef();
    try {
      recycler_(texture_, width_, height_, reusable);
    } catch (...) {
      texture_->Release();
    }
  }

  void Transfer() noexcept { armed_ = false; }
  const VideoEncoder::TextureRecycler& Recycler() const noexcept { return recycler_; }

 private:
  ID3D11Texture2D* texture_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  VideoEncoder::TextureRecycler recycler_;
  bool armed_ = true;
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
                              Error& error, bool inject_runtime_failure) {
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
  runtime_rejected_encoders_.clear();
  runtime_recovery_attempted_encoders_.clear();
  hardware_device_failure_.clear();
  runtime_failed_encoder_.clear();
  runtime_failure_details_.clear();
#if defined(KLIP_ENABLE_TEST_HOOKS)
  inject_runtime_failure_ = inject_runtime_failure;
  injected_runtime_failures_remaining_ = inject_runtime_failure ? 2U : 0U;
#else
  (void)inject_runtime_failure;
#endif
  state_.SetEncoderStatus("Checking available video encoders");
  const bool software_only =
      !config_.encoder_preferences.empty() &&
      std::all_of(config_.encoder_preferences.begin(), config_.encoder_preferences.end(),
                  [](const auto& name) { return name == "h264_mf_software"; });
  if (software_only) {
    logger_.Info("Video encoder policy is software-only; skipping D3D11 encoder initialization");
    return true;
  }
  Error hardware_error;
  if (!CreateHardwareDevice(hardware_error)) {
    hardware_device_failure_ = hardware_error.ToString();
    logger_.Warning(hardware_error.ToString() +
                    "; hardware encoders will be skipped and software fallback will be tried");
  }
  return true;
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

void VideoEncoder::RequestKeyframe() {
  std::scoped_lock lock(mutex_);
  force_keyframe_ = true;
}

bool VideoEncoder::Prepare(std::uint32_t width, std::uint32_t height, Error& error) {
  std::scoped_lock lock(mutex_);
  return EnsureOpen(width, height, error);
}

bool VideoEncoder::Encode(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height,
                          std::int64_t pts_100ns, TextureRecycler recycler, Error& error) {
  std::scoped_lock lock(mutex_);
  TextureReturnGuard return_texture(texture, width, height, std::move(recycler));
  if (!EnsureOpen(width, height, error) || texture == nullptr) {
    return false;
  }

#if defined(KLIP_ENABLE_TEST_HOOKS)
  if (inject_runtime_failure_ && active_encoder_ == "h264_nvenc" &&
      injected_runtime_failures_remaining_ > 0 &&
      submitted_frames_.load(std::memory_order_acquire) >= 120) {
    --injected_runtime_failures_remaining_;
    error = Error{ErrorComponent::kVideoEncoder, "submit video frame",
                  "test-only injected NVENC runtime failure"};
    logger_.Warning("TEST: injecting NVENC runtime failure");
    RecoverFromRuntimeFailure(error);
    return false;
  }
#endif

  std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
  if (!frame) {
    error = Error{ErrorComponent::kVideoEncoder, "allocate frame", "av_frame_alloc returned null"};
    return false;
  }
  frame->format = AV_PIX_FMT_D3D11;
  frame->width = static_cast<int>(width);
  frame->height = static_cast<int>(height);
  constexpr AVRational kHundredNanoseconds{1, 10'000'000};
  const auto codec_pts = av_rescale_q(pts_100ns, kHundredNanoseconds, codec_->time_base);
  frame->pts = codec_pts;
  if (force_keyframe_) frame->pict_type = AV_PICTURE_TYPE_I;
  if (codec_->pix_fmt == AV_PIX_FMT_D3D11) {
    const auto found = std::find_if(registered_textures_.begin(), registered_textures_.end(),
                                    [texture](const auto& item) { return item.get() == texture; });
    if (found == registered_textures_.end()) {
      if (registered_textures_.size() >= 64) {
        error = Error{ErrorComponent::kVideoEncoder, "retain encoder texture",
                      "encoder texture pool exhausted; input surfaces must be reused"};
        return false;
      }
      winrt::com_ptr<ID3D11Texture2D> retained;
      retained.copy_from(texture);
      registered_textures_.push_back(std::move(retained));
    }
    if (hardware_frames_ != nullptr) {
      frame->hw_frames_ctx = av_buffer_ref(hardware_frames_);
    }

    auto* cookie = new RecycleCookie{return_texture.Recycler(), texture, width, height};
    texture->AddRef();
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
    return_texture.Transfer();
  } else {
    frame->format = AV_PIX_FMT_NV12;
    const auto allocate_result = av_frame_get_buffer(frame.get(), 32);
    if (allocate_result < 0) {
      error = MakeFfmpegError(ErrorComponent::kVideoEncoder, "allocate fallback video frame",
                              allocate_result, active_encoder_);
      return false;
    }

    if (!staging_texture_ || staging_width_ != width || staging_height_ != height) {
      D3D11_TEXTURE2D_DESC staging{};
      staging.Width = width;
      staging.Height = height;
      staging.MipLevels = 1;
      staging.ArraySize = 1;
      staging.Format = DXGI_FORMAT_NV12;
      staging.SampleDesc.Count = 1;
      staging.Usage = D3D11_USAGE_STAGING;
      staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      staging_texture_ = nullptr;
      const auto create_result =
          device_->CreateTexture2D(&staging, nullptr, staging_texture_.put());
      if (FAILED(create_result)) {
        error =
            MakeHresultError(ErrorComponent::kVideoEncoder, "allocate fallback readback texture",
                             create_result, active_encoder_);
        return false;
      }
      staging_width_ = width;
      staging_height_ = height;
    }

    context_->CopyResource(staging_texture_.get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const auto map_result = context_->Map(staging_texture_.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(map_result)) {
      error = MakeHresultError(ErrorComponent::kVideoEncoder, "read fallback video frame",
                               map_result, active_encoder_);
      // CopyResource was submitted, but Map did not prove completion. Retire the surface instead
      // of allowing a later conversion to overwrite it while the readback may still be pending.
      return_texture.Return(false);
      return false;
    }
    const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
    for (std::uint32_t row = 0; row < height; ++row) {
      std::copy_n(source + static_cast<std::size_t>(row) * mapped.RowPitch, width,
                  frame->data[0] + static_cast<std::size_t>(row) * frame->linesize[0]);
    }
    const auto* chroma = source + static_cast<std::size_t>(mapped.RowPitch) * height;
    for (std::uint32_t row = 0; row < height / 2; ++row) {
      std::copy_n(chroma + static_cast<std::size_t>(row) * mapped.RowPitch, width,
                  frame->data[1] + static_cast<std::size_t>(row) * frame->linesize[1]);
    }
    context_->Unmap(staging_texture_.get(), 0);

    // The GPU copy is complete before Map returns. Return this texture to the capture pool now;
    // software encoding owns the AVFrame buffer rather than the D3D11 allocation.
    return_texture.Return(true);
  }

  auto result = avcodec_send_frame(codec_, frame.get());
  // Hardware encoders are asynchronous. Drain and retry bounded transient backpressure instead
  // of immediately turning one busy frame into a permanently stalled capture session.
  for (int retry = 0; result == AVERROR(EAGAIN) && retry < 3; ++retry) {
    const auto drain_result = Drain(codec_pts);
    if (drain_result < 0) {
      result = drain_result;
      break;
    }
    std::this_thread::yield();
    result = avcodec_send_frame(codec_, frame.get());
  }
  if (result < 0) {
    error = MakeFfmpegError(ErrorComponent::kVideoEncoder, "submit video frame", result,
                            active_encoder_);
    RecoverFromRuntimeFailure(error);
    return false;
  }
  submitted_frames_.fetch_add(1, std::memory_order_release);
  force_keyframe_ = false;
  const auto drain_result = Drain(codec_pts);
  if (drain_result < 0) {
    error = MakeFfmpegError(ErrorComponent::kVideoEncoder, "receive encoded packet", drain_result,
                            active_encoder_);
    RecoverFromRuntimeFailure(error);
    return false;
  }
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
    cookie->recycler(cookie->texture, cookie->width, cookie->height, true);
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
    av_buffer_unref(&hardware_device_);
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
  d3d->BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
#ifdef D3D11_BIND_VIDEO_ENCODER
  d3d->BindFlags |= D3D11_BIND_VIDEO_ENCODER;
#endif
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
  const auto now = std::chrono::steady_clock::now();
  if (codec_ == nullptr && width == failed_width_ && height == failed_height_ &&
      now < next_open_attempt_) {
    error = last_open_error_;
    return false;
  }
  const bool replacing_codec = codec_ != nullptr;
  FlushLocked();
  if (replacing_codec) router_.ResetTimeline();
  ReleaseCodec();
  if (hardware_device_ != nullptr) {
    Error frames_error;
    if (!CreateFramesContext(width, height, frames_error)) {
      logger_.Warning(frames_error.ToString() + "; trying direct D3D11 texture input");
    }
  }

  std::vector<std::string> rejected_attempts;
  std::string rejected_candidate;
  std::string rejected_reason;
  for (const auto& candidate : BuildPreference()) {
    Error candidate_error;
    if (TryOpen(candidate, width, height, candidate_error)) {
      next_open_attempt_ = {};
      failed_width_ = 0;
      failed_height_ = 0;
      last_open_error_ = {};
      state_.SetEncoder(candidate);
      state_.ClearError(ErrorComponent::kVideoEncoder);
      logger_.Info("Selected video encoder: " + candidate);
      if (candidate == runtime_failed_encoder_ &&
          std::find(runtime_rejected_encoders_.begin(), runtime_rejected_encoders_.end(),
                    candidate) == runtime_rejected_encoders_.end()) {
        logger_.Info("Video encoder recovered after restart: " + candidate);
        runtime_failed_encoder_.clear();
        runtime_failure_details_.clear();
      }
      if (rejected_candidate.empty() && hardware_device_failure_.empty() &&
          runtime_failure_details_.empty()) {
        state_.SetEncoderStatus({});
      } else {
        std::string reason = !runtime_failure_details_.empty() ? runtime_failure_details_
                             : !rejected_candidate.empty()
                                 ? rejected_candidate + " was unavailable: " + rejected_reason
                                 : hardware_device_failure_;
        const bool software_fallback = candidate == "h264_mf_software";
        const std::string status =
            std::string(software_fallback ? "Software compatibility fallback active: "
                                          : "Encoder fallback active: ") +
            candidate + " selected after " + reason +
            (software_fallback
                 ? ". CPU usage may be higher and affect frame pacing. For more headroom, enable "
                   "Performance mode (720p/60) or lower resolution/FPS."
                 : ".");
        state_.SetEncoderStatus(status);
        logger_.Warning(status);
      }
      return true;
    }
    logger_.Warning(candidate_error.ToString());
    rejected_attempts.push_back(candidate + ": " + candidate_error.ToString());
    if (rejected_candidate.empty()) {
      rejected_candidate = candidate;
      rejected_reason = candidate_error.ToString();
    }
  }
  error = Error{ErrorComponent::kVideoEncoder, "select encoder",
                FormatEncoderSelectionFailure(rejected_attempts)};
  last_open_error_ = error;
  failed_width_ = width;
  failed_height_ = height;
  next_open_attempt_ = now + std::chrono::seconds(5);
  return false;
}

bool VideoEncoder::TryOpen(const std::string& name, std::uint32_t width, std::uint32_t height,
                           Error& error) {
  const bool software_fallback = name == "h264_mf_software";
  if (!software_fallback && hardware_device_ == nullptr) {
    error = Error{ErrorComponent::kVideoEncoder,
                  "open encoder",
                  "D3D11 hardware encoding is unavailable; selecting the software fallback",
                  {},
                  {},
                  name};
    return false;
  }
  const char* codec_name = software_fallback ? "h264_mf" : name.c_str();
  const auto* encoder = avcodec_find_encoder_by_name(codec_name);
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
  const auto tuning = BuildEncoderTuning(name, config_.target_fps, config_.encoder_quality);
  context->pix_fmt = software_fallback ? AV_PIX_FMT_NV12 : AV_PIX_FMT_D3D11;
  context->sw_pix_fmt = AV_PIX_FMT_NV12;
  context->time_base = AVRational{1, static_cast<int>(config_.target_fps)};
  context->pkt_timebase = context->time_base;
  context->framerate = AVRational{static_cast<int>(config_.target_fps), 1};
  context->gop_size = tuning.gop_frames;
  context->max_b_frames = tuning.max_b_frames;
  context->thread_count = 1;
  if (tuning.low_delay) context->flags |= AV_CODEC_FLAG_LOW_DELAY;
  context->bit_rate = config_.video_bitrate;
  context->rc_max_rate = config_.video_bitrate;
  context->rc_buffer_size = static_cast<int>(
      std::min<std::int64_t>(config_.video_bitrate, std::numeric_limits<int>::max()));
  context->profile = AV_PROFILE_H264_HIGH;
  context->color_range = AVCOL_RANGE_MPEG;
  context->color_primaries = AVCOL_PRI_BT709;
  context->color_trc = AVCOL_TRC_BT709;
  context->colorspace = AVCOL_SPC_BT709;
  context->chroma_sample_location = AVCHROMA_LOC_LEFT;
  context->sample_aspect_ratio = AVRational{1, 1};
  if (!software_fallback) {
    context->hw_device_ctx = av_buffer_ref(hardware_device_);
    if (hardware_frames_ != nullptr) {
      context->hw_frames_ctx = av_buffer_ref(hardware_frames_);
    }
  }
  AVDictionary* options = nullptr;
  for (const auto& [key, value] : tuning.options)
    av_dict_set(&options, key.c_str(), value.c_str(), 0);
  const auto result = avcodec_open2(context, encoder, &options);
  for (const AVDictionaryEntry* option = nullptr;
       (option = av_dict_get(options, "", option, AV_DICT_IGNORE_SUFFIX)) != nullptr;) {
    logger_.Warning("Encoder '" + name + "' did not accept tuning option '" + option->key +
                    "'; using its backend default for this option");
  }
  av_dict_free(&options);
  if (result < 0) {
    error = MakeFfmpegError(ErrorComponent::kVideoEncoder, "open encoder", result, name);
    avcodec_free_context(&context);
    return false;
  }
  codec_ = context;
  active_encoder_ = name;
  width_ = width;
  height_ = height;
  last_pts_ = AV_NOPTS_VALUE;
  last_dts_ = AV_NOPTS_VALUE;
  force_keyframe_ = true;
  logged_first_packet_ = false;
  logged_first_keyframe_ = false;
  return true;
}

int VideoEncoder::Drain(std::int64_t fallback_pts) {
  PacketPtr packet(av_packet_alloc());
  if (!packet || codec_ == nullptr) {
    return codec_ == nullptr ? 0 : AVERROR(ENOMEM);
  }
  for (;;) {
    const auto result = avcodec_receive_packet(codec_, packet.get());
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
      return 0;
    }
    if (result < 0) {
      return result;
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
    emitted_frames_.fetch_add(1, std::memory_order_release);
    av_packet_unref(packet.get());
  }
}

void VideoEncoder::RecoverFromRuntimeFailure(const Error& failure) {
  if (active_encoder_.empty()) return;
  const auto failed_encoder = active_encoder_;
  const auto recovery = std::find(runtime_recovery_attempted_encoders_.begin(),
                                  runtime_recovery_attempted_encoders_.end(), failed_encoder);
  if (recovery == runtime_recovery_attempted_encoders_.end()) {
    runtime_recovery_attempted_encoders_.push_back(failed_encoder);
    logger_.Warning("Video encoder failed at runtime; restarting once: " + failed_encoder);
    runtime_failed_encoder_ = failed_encoder;
    runtime_failure_details_ = failed_encoder + " / " + failure.ToString();
    state_.SetEncoderStatus("Encoder " + failed_encoder + " failed; retrying once");
  } else if (std::find(runtime_rejected_encoders_.begin(), runtime_rejected_encoders_.end(),
                       failed_encoder) == runtime_rejected_encoders_.end()) {
    runtime_rejected_encoders_.push_back(failed_encoder);
    logger_.Warning("Video encoder failed again; trying fallback: " + failed_encoder);
    runtime_failure_details_ = failed_encoder + " / " + failure.ToString();
    state_.SetEncoderStatus("Encoder " + failed_encoder + " failed again; switching to fallback");
  }
  // An AVCodecContext is not reusable after ENOMEM/EINVAL from a hardware backend. Clear replay
  // history so dependent pictures from the failed codec cannot be clipped. Keep recording alive:
  // all supported encoders still emit H.264, and the reopened codec will begin with a new keyframe.
  router_.ResetTimeline(false);
  ReleaseCodec();
}

void VideoEncoder::NormalizeTimestamps(AVPacket* packet, std::int64_t fallback_pts) {
  constexpr std::int64_t duration = 1;
  auto pts = packet->pts == AV_NOPTS_VALUE ? fallback_pts : packet->pts;
  // PTS is intentionally non-monotonic in decode order when B-frames are enabled. Only
  // repair presentation timestamps on the no-reorder performance path; always keep DTS
  // strictly increasing for the MP4 muxer.
  if (codec_ != nullptr && codec_->max_b_frames == 0 && last_pts_ != AV_NOPTS_VALUE &&
      pts <= last_pts_) {
    pts = last_pts_ + duration;
  }
  auto dts = packet->dts == AV_NOPTS_VALUE ? pts : packet->dts;
  if (last_dts_ != AV_NOPTS_VALUE && dts <= last_dts_) {
    dts = last_dts_ + duration;
  }
  packet->pts = pts;
  packet->dts = dts;
  // Klip's scheduler is CFR. Some hardware encoders report a widened duration for the first
  // reordered packets even though each submitted frame represents exactly one schedule tick;
  // preserving that value makes short replay clips advertise a lower average frame rate.
  packet->duration = duration;
  last_pts_ = last_pts_ == AV_NOPTS_VALUE ? pts : std::max(last_pts_, pts);
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
  registered_textures_.clear();
  if (hardware_frames_ != nullptr) {
    av_buffer_unref(&hardware_frames_);
  }
  staging_texture_ = nullptr;
  staging_width_ = 0;
  staging_height_ = 0;
  width_ = 0;
  height_ = 0;
  active_encoder_.clear();
  next_open_attempt_ = {};
  failed_width_ = 0;
  failed_height_ = 0;
  last_open_error_ = {};
  state_.SetEncoder({});
}

std::vector<std::string> VideoEncoder::BuildPreference() const {
  auto preference = FilterEncodersForAdapter(
      PrioritizeEncoders(config_.encoder_preferences, adapter_vendor_id_), adapter_vendor_id_);
  preference.erase(std::remove_if(preference.begin(), preference.end(),
                                  [&](const auto& name) {
                                    return std::find(runtime_rejected_encoders_.begin(),
                                                     runtime_rejected_encoders_.end(),
                                                     name) != runtime_rejected_encoders_.end();
                                  }),
                   preference.end());
  // Media Foundation provides both a vendor-neutral hardware fallback and a last-resort
  // software transform. Keep both available because a driver-specific backend can open and
  // still reject native capture textures at runtime.
  if (std::find(preference.begin(), preference.end(), "h264_mf") == preference.end() &&
      std::find(runtime_rejected_encoders_.begin(), runtime_rejected_encoders_.end(), "h264_mf") ==
          runtime_rejected_encoders_.end())
    preference.push_back("h264_mf");
  if (std::find(preference.begin(), preference.end(), "h264_mf_software") == preference.end() &&
      std::find(runtime_rejected_encoders_.begin(), runtime_rejected_encoders_.end(),
                "h264_mf_software") == runtime_rejected_encoders_.end())
    preference.push_back("h264_mf_software");
  return preference;
}

}  // namespace klip
