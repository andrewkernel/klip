#include "klip/media/mp4_muxer.h"

#include <Windows.h>

#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/mathematics.h>
}

namespace klip {
namespace {

constexpr AVRational kHundredNanoseconds{1, 10'000'000};

}  // namespace

Mp4Muxer::~Mp4Muxer() noexcept { Abort(); }

bool Mp4Muxer::Open(const std::filesystem::path& output_path, const CodecSnapshot& video,
                    const CodecSnapshot* audio, ErrorComponent component, Error& error) {
  Abort();
  if (!video.parameters) {
    error = Error{component, "open MP4", "video codec parameters are unavailable"};
    return false;
  }

  component_ = component;
  output_path_ = output_path;
  temporary_path_ = output_path;
  temporary_path_ += ".partial";
  std::error_code ignored;
  std::filesystem::remove(temporary_path_, ignored);

  const auto utf8 = Utf8(temporary_path_);
  auto result = avformat_alloc_output_context2(&format_, nullptr, "mp4", utf8.c_str());
  if (result < 0 || format_ == nullptr) {
    error = MakeFfmpegError(component_, "allocate MP4 output", result, utf8);
    Abort();
    return false;
  }

  video_stream_ = avformat_new_stream(format_, nullptr);
  if (video_stream_ == nullptr ||
      avcodec_parameters_copy(video_stream_->codecpar, video.parameters.get()) < 0) {
    error = Error{component_, "create MP4 video stream",
                  "failed to copy video codec parameters"};
    Abort();
    return false;
  }
  video_stream_->codecpar->codec_tag = 0;
  video_stream_->time_base = video.time_base;

  if (audio != nullptr && audio->parameters) {
    audio_stream_ = avformat_new_stream(format_, nullptr);
    if (audio_stream_ == nullptr ||
        avcodec_parameters_copy(audio_stream_->codecpar, audio->parameters.get()) < 0) {
      error = Error{component_, "create MP4 audio stream",
                    "failed to copy audio codec parameters"};
      Abort();
      return false;
    }
    audio_stream_->codecpar->codec_tag = 0;
    audio_stream_->time_base = audio->time_base;
  }

  if ((format_->oformat->flags & AVFMT_NOFILE) == 0) {
    result = avio_open(&format_->pb, utf8.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) {
      error = MakeFfmpegError(component_, "open temporary MP4", result, utf8);
      Abort();
      return false;
    }
  }

  AVDictionary* options = nullptr;
  av_dict_set(&options, "movflags", "+faststart", 0);
  result = avformat_write_header(format_, &options);
  av_dict_free(&options);
  if (result < 0) {
    error = MakeFfmpegError(component_, "write MP4 header", result, utf8);
    Abort();
    return false;
  }
  header_written_ = true;
  return true;
}

bool Mp4Muxer::Write(const EncodedPacket& encoded, std::int64_t base_timestamp_100ns,
                     Error& error) {
  AVStream* stream = encoded.kind == StreamKind::kVideo ? video_stream_ : audio_stream_;
  if (format_ == nullptr || !encoded.packet || stream == nullptr) return true;

  PacketPtr packet(av_packet_clone(encoded.packet.get()));
  if (!packet) {
    error = Error{component_, "clone encoded packet", "av_packet_clone returned null"};
    return false;
  }
  const auto base_source =
      av_rescale_q(base_timestamp_100ns, kHundredNanoseconds, encoded.time_base);
  if (packet->pts != AV_NOPTS_VALUE)
    packet->pts = std::max<std::int64_t>(0, packet->pts - base_source);
  if (packet->dts != AV_NOPTS_VALUE)
    packet->dts = std::max<std::int64_t>(0, packet->dts - base_source);
  av_packet_rescale_ts(packet.get(), encoded.time_base, stream->time_base);
  if (packet->pts == AV_NOPTS_VALUE) packet->pts = packet->dts;
  if (packet->dts == AV_NOPTS_VALUE) packet->dts = packet->pts;

  auto& last_dts = encoded.kind == StreamKind::kVideo ? last_video_dts_ : last_audio_dts_;
  if (packet->dts <= last_dts) packet->dts = last_dts + 1;
  if (packet->pts < packet->dts) packet->pts = packet->dts;
  last_dts = packet->dts;
  packet->stream_index = stream->index;
  packet->pos = -1;

  const auto result = av_interleaved_write_frame(format_, packet.get());
  if (result < 0) {
    error = MakeFfmpegError(component_, "write MP4 packet", result, Utf8(temporary_path_));
    return false;
  }
  return true;
}

bool Mp4Muxer::Finalize(Error& error) {
  if (format_ == nullptr || !header_written_) {
    error = Error{component_, "finalize MP4", "the output file was not opened"};
    return false;
  }
  const auto result = av_write_trailer(format_);
  header_written_ = false;
  if (result < 0) {
    error = MakeFfmpegError(component_, "write MP4 trailer", result, Utf8(temporary_path_));
    Abort();
    return false;
  }
  if (!CloseFile(error)) {
    Abort();
    return false;
  }

  std::error_code publication_error;
  std::filesystem::rename(temporary_path_, output_path_, publication_error);
  if (publication_error) {
    error = Error{component_, "publish completed MP4", publication_error.message(),
                  publication_error.value(), publication_error.message(), output_path_.string()};
    Abort();
    return false;
  }
  published_ = true;
  return true;
}

void Mp4Muxer::Abort() noexcept {
  if (format_ != nullptr) {
    if ((format_->oformat->flags & AVFMT_NOFILE) == 0 && format_->pb != nullptr)
      avio_closep(&format_->pb);
    avformat_free_context(format_);
  }
  format_ = nullptr;
  video_stream_ = nullptr;
  audio_stream_ = nullptr;
  header_written_ = false;
  if (!published_ && !temporary_path_.empty()) {
    std::error_code ignored;
    std::filesystem::remove(temporary_path_, ignored);
  }
  output_path_.clear();
  temporary_path_.clear();
  last_video_dts_ = -1;
  last_audio_dts_ = -1;
  published_ = false;
}

std::string Mp4Muxer::Utf8(const std::filesystem::path& path) {
  const auto value = path.wstring();
  const auto required =
      WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (required <= 0) return {};
  std::string output(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), required, nullptr, nullptr);
  output.pop_back();
  return output;
}

bool Mp4Muxer::CloseFile(Error& error) {
  if (format_ == nullptr) return true;
  int result = 0;
  if ((format_->oformat->flags & AVFMT_NOFILE) == 0 && format_->pb != nullptr)
    result = avio_closep(&format_->pb);
  avformat_free_context(format_);
  format_ = nullptr;
  video_stream_ = nullptr;
  audio_stream_ = nullptr;
  if (result < 0) {
    error = MakeFfmpegError(component_, "close MP4 file", result, Utf8(temporary_path_));
    return false;
  }
  return true;
}

}  // namespace klip
