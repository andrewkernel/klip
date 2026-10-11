#include "klip/clips/trimmer.h"

#include "klip/core/path_text.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace klip {
namespace {
struct Input {
  AVFormatContext* value = nullptr;
  HANDLE held = INVALID_HANDLE_VALUE;
  explicit Input(const std::filesystem::path& source,
                 const std::stop_token& stop) {
    held = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (held == INVALID_HANDLE_VALUE)
      throw std::runtime_error(
          "Clip is unavailable or still being written. Wait for it to finish.");
    value = avformat_alloc_context();
    if (!value) {
      CloseHandle(held);
      held = INVALID_HANDLE_VALUE;
      throw std::runtime_error("Cannot allocate media reader");
    }
    value->interrupt_callback = {
        [](void* token) {
          return static_cast<const std::stop_token*>(token)->stop_requested()
                     ? 1
                     : 0;
        },
        const_cast<std::stop_token*>(&stop)};
    auto path = PathToUtf8(source);
    if (avformat_open_input(&value, path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(value, nullptr) < 0) {
      avformat_close_input(&value);
      CloseHandle(held);
      held = INVALID_HANDLE_VALUE;
      throw std::runtime_error("Cannot read this clip");
    }
  }
  ~Input() {
    avformat_close_input(&value);
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
  }
};
struct Packet {
  AVPacket* value = av_packet_alloc();
  ~Packet() { av_packet_free(&value); }
};
struct Decoder {
  AVCodecContext* value = nullptr;
  ~Decoder() { avcodec_free_context(&value); }
};
struct Frame {
  AVFrame* value = av_frame_alloc();
  ~Frame() { av_frame_free(&value); }
};
struct Scaler {
  SwsContext* value = nullptr;
  ~Scaler() { sws_freeContext(value); }
};
struct Output {
  AVFormatContext* value = nullptr;
  ~Output() {
    if (value) {
      if (value->pb) avio_closep(&value->pb);
      avformat_free_context(value);
    }
  }
};
int Video(AVFormatContext* input) {
  const auto i =
      av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (i < 0) throw std::runtime_error("No video stream in this clip");
  return i;
}
double Base(AVFormatContext* input) {
  return input->start_time == AV_NOPTS_VALUE
             ? 0
             : static_cast<double>(input->start_time) / AV_TIME_BASE;
}
double Time(const AVPacket* p, const AVStream* stream) {
  const auto t = p->pts == AV_NOPTS_VALUE ? p->dts : p->pts;
  return t == AV_NOPTS_VALUE
             ? -1
             : static_cast<double>(t) * av_q2d(stream->time_base);
}
void Priority() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
}
std::filesystem::path UniqueOutput(const std::filesystem::path& source) {
  for (unsigned i = 1; i <= 10000; ++i) {
    auto result =
        source.parent_path() /
        (source.stem().wstring() + L"-trimmed-" + std::to_wstring(i) + L".mkv");
    if (!std::filesystem::exists(result)) return result;
  }
  throw std::runtime_error("Too many trimmed copies in this folder");
}
}  // namespace
void ClipTrimmer::Publish(TrimView view) {
  {
    std::lock_guard lock(mutex_);
    view_ = std::move(view);
  }
  if (window_) PostMessageW(window_, WM_APP + 57, 0, 0);
}
TrimView ClipTrimmer::Snapshot() const {
  std::lock_guard lock(mutex_);
  return view_;
}
bool ClipTrimmer::Inspect(const std::filesystem::path& source) {
  auto view = Snapshot();
  if (view.busy) return false;
  if (worker_.joinable()) worker_.join();
  view = {};
  view.source = source;
  view.busy = true;
  view.message = "Reading clip timeline...";
  Publish(view);
  worker_ = std::jthread([this, view](std::stop_token stop) mutable {
    try {
      Priority();
      std::error_code error;
      const auto bytes = std::filesystem::file_size(view.source, error);
      if (error || bytes > 4ULL * 1024 * 1024 * 1024)
        throw std::runtime_error("Clip is unavailable or larger than 4 GiB");
      Input input(view.source, stop);
      auto* format = input.value;
      const auto video = Video(format);
      const auto base = Base(format);
      Packet packet;
      if (!packet.value)
        throw std::runtime_error("Cannot allocate media packet");
      double end = 0;
      unsigned packets = 0;
      int result = 0;
      while (!stop.stop_requested() &&
             (result = av_read_frame(format, packet.value)) >= 0) {
        if (++packets > 2000000)
          throw std::runtime_error("Clip exceeds the editor's packet limit");
        if (packet.value->stream_index == video) {
          const auto time = Time(packet.value, format->streams[video]) - base;
          if (time >= 0) {
            end = std::max(
                end, time + static_cast<double>(packet.value->duration) *
                                av_q2d(format->streams[video]->time_base));
            if (packet.value->flags & AV_PKT_FLAG_KEY)
              view.keyframes.push_back(time);
          }
        }
        av_packet_unref(packet.value);
      }
      if (stop.stop_requested()) throw std::runtime_error("Reading cancelled");
      if (result != AVERROR_EOF)
        throw std::runtime_error("Clip could not be read completely");
      if (end <= 0 || end > 602 || view.keyframes.empty())
        throw std::runtime_error(
            "Editor supports finalized clips up to ten minutes with video "
            "keyframes");
      std::sort(view.keyframes.begin(), view.keyframes.end());
      view.duration = end;
      view.loaded = true;
      view.busy = false;
      view.message = "Choose the part to keep. Your original stays unchanged.";
      Publish(view);
    } catch (const std::exception& e) {
      view.busy = false;
      view.message = e.what();
      Publish(view);
    }
  });
  return true;
}
bool ClipTrimmer::Preview(double position) {
  auto view = Snapshot();
  if (view.busy || !view.loaded || !std::isfinite(position) || position < 0 ||
      position > view.duration)
    return false;
  if (worker_.joinable()) worker_.join();
  view.busy = true;
  view.message = "Loading preview...";
  Publish(view);
  worker_ = std::jthread([this, view, position](std::stop_token stop) mutable {
    try {
      Priority();
      Input input(view.source, stop);
      const auto video = Video(input.value);
      auto* stream = input.value->streams[video];
      const auto base = Base(input.value);
      const auto* codec = avcodec_find_decoder(stream->codecpar->codec_id);
      Decoder decoder;
      decoder.value = avcodec_alloc_context3(codec);
      if (!decoder.value ||
          avcodec_parameters_to_context(decoder.value, stream->codecpar) < 0)
        throw std::runtime_error("Cannot prepare clip preview");
      decoder.value->thread_count = 1;
      if (avcodec_open2(decoder.value, codec, nullptr) < 0)
        throw std::runtime_error("Cannot decode this clip");
      const auto start = TrimKeyframeStart(view.keyframes, position);
      if (av_seek_frame(input.value, video,
                        static_cast<std::int64_t>((start + base) /
                                                  av_q2d(stream->time_base)),
                        AVSEEK_FLAG_BACKWARD) < 0)
        throw std::runtime_error("Cannot seek this clip");
      Packet packet;
      Frame frame;
      if (!packet.value || !frame.value)
        throw std::runtime_error("Cannot allocate preview frame");
      bool found = false;
      unsigned decoded = 0;
      while (!stop.stop_requested() && !found &&
             av_read_frame(input.value, packet.value) >= 0) {
        if (packet.value->stream_index == video) {
          if (avcodec_send_packet(decoder.value, packet.value) < 0)
            throw std::runtime_error("Preview decoder rejected a video packet");
          while (avcodec_receive_frame(decoder.value, frame.value) >= 0) {
            const double time =
                static_cast<double>(frame.value->best_effort_timestamp) *
                    av_q2d(stream->time_base) -
                base;
            if (++decoded > 1500)
              throw std::runtime_error("Preview seek exceeded its work limit");
            if (time + 0.001 >= position || time >= view.duration - 0.05) {
              if (frame.value->width < 1 || frame.value->width > 8192 ||
                  frame.value->height < 1 || frame.value->height > 8192)
                throw std::runtime_error("Unsupported preview dimensions");
              auto image = std::make_shared<TrimImage>();
              image->width = std::min(640, frame.value->width);
              image->height = std::max(
                  1, static_cast<int>(static_cast<double>(frame.value->height) *
                                      image->width / frame.value->width));
              image->rgba.resize(static_cast<std::size_t>(image->width) *
                                 image->height * 4);
              Scaler scaler;
              scaler.value = sws_getContext(
                  frame.value->width, frame.value->height,
                  static_cast<AVPixelFormat>(frame.value->format), image->width,
                  image->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                  nullptr, nullptr);
              if (!scaler.value)
                throw std::runtime_error("Cannot convert preview");
              std::array<unsigned char*, 4> data{image->rgba.data(), nullptr,
                                                 nullptr, nullptr};
              std::array<int, 4> stride{image->width * 4, 0, 0, 0};
              if (sws_scale(scaler.value, frame.value->data,
                            frame.value->linesize, 0, frame.value->height,
                            data.data(), stride.data()) <= 0)
                throw std::runtime_error("Preview conversion failed");
              view.image = std::move(image);
              found = true;
              break;
            }
            av_frame_unref(frame.value);
          }
        }
        av_packet_unref(packet.value);
      }
      if (stop.stop_requested()) throw std::runtime_error("Preview cancelled");
      if (!found) throw std::runtime_error("No preview frame at this position");
      view.busy = false;
      view.message = "Choose the part to keep. Your original stays unchanged.";
      Publish(view);
    } catch (const std::exception& e) {
      view.busy = false;
      view.message = e.what();
      Publish(view);
    }
  });
  return true;
}
bool ClipTrimmer::Export(double start, double end) {
  auto view = Snapshot();
  if (view.busy || !view.loaded || !ValidTrimRange(start, end, view.duration))
    return false;
  if (worker_.joinable()) worker_.join();
  view.busy = true;
  view.message = "Saving lossless trimmed copy...";
  Publish(view);
  worker_ = std::jthread([this, view, start,
                          end](std::stop_token stop) mutable {
    std::filesystem::path partial;
    bool owns_partial = false;
    try {
      Priority();
      Input input(view.source, stop);
      auto* format = input.value;
      const auto video = Video(format);
      const auto base = Base(format);
      const auto cut = TrimKeyframeStart(view.keyframes, start);
      const auto cut_end = TrimKeyframeEnd(view.keyframes, end, view.duration);
      view.output = UniqueOutput(view.source);
      partial = view.output;
      partial += L"." + std::to_wstring(GetCurrentProcessId()) + L".partial";
      // Exclusive creation and no replace on publish; another clip is never
      // overwritten.
      HANDLE reservation =
          CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
      if (reservation == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create trimmed copy");
      owns_partial = true;
      CloseHandle(reservation);
      Output output;
      const auto path = PathToUtf8(partial);
      if (avformat_alloc_output_context2(&output.value, nullptr, "matroska",
                                         path.c_str()) < 0 ||
          !output.value)
        throw std::runtime_error("Cannot prepare trimmed output");
      std::vector<int> mapping(format->nb_streams, -1);
      for (unsigned i = 0; i < format->nb_streams; ++i) {
        auto* stream = format->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
          continue;
        auto* target = avformat_new_stream(output.value, nullptr);
        if (!target ||
            avcodec_parameters_copy(target->codecpar, stream->codecpar) < 0)
          throw std::runtime_error("Cannot copy stream settings");
        target->codecpar->codec_tag = 0;
        target->time_base = stream->time_base;
        av_dict_copy(&target->metadata, stream->metadata, 0);
        target->disposition = stream->disposition;
        mapping[i] = target->index;
      }
      if (avio_open(&output.value->pb, path.c_str(), AVIO_FLAG_WRITE) < 0 ||
          avformat_write_header(output.value, nullptr) < 0)
        throw std::runtime_error("Cannot write trimmed file");
      if (av_seek_frame(
              format, video,
              static_cast<std::int64_t>(
                  (cut + base) / av_q2d(format->streams[video]->time_base)),
              AVSEEK_FLAG_BACKWARD) < 0)
        throw std::runtime_error("Cannot seek clip for export");
      Packet packet;
      if (!packet.value)
        throw std::runtime_error("Cannot allocate trim packet");
      bool started = false;
      double origin = 0, max_end = 0;
      unsigned frames = 0;
      int result = 0;
      // Matroska can omit the first reordered DTS after seeking. Reconstruct
      // only that bounded leading prefix from the next known decode timestamp
      // and cadence.
      const auto rate =
          av_guess_frame_rate(format, format->streams[video], nullptr);
      const auto step = rate.num > 0 && rate.den > 0
                            ? av_rescale_q(1, av_inv_q(rate),
                                           format->streams[video]->time_base)
                            : 0;
      std::int64_t leading_dts = AV_NOPTS_VALUE;
      unsigned missing = 0;
      for (unsigned scanned = 0;
           scanned < 128 && av_read_frame(format, packet.value) >= 0;
           ++scanned) {
        if (packet.value->stream_index == video) {
          if (packet.value->dts != AV_NOPTS_VALUE) {
            leading_dts =
                packet.value->dts - static_cast<std::int64_t>(missing) * step;
            av_packet_unref(packet.value);
            break;
          }
          if (++missing > 16)
            throw std::runtime_error(
                "Cannot reconstruct clip decode timestamps");
        }
        av_packet_unref(packet.value);
      }
      if (missing && (leading_dts == AV_NOPTS_VALUE || step <= 0))
        throw std::runtime_error("Cannot determine clip decode cadence");
      if (av_seek_frame(
              format, video,
              static_cast<std::int64_t>(
                  (cut + base) / av_q2d(format->streams[video]->time_base)),
              AVSEEK_FLAG_BACKWARD) < 0)
        throw std::runtime_error("Cannot reset clip seek");
      while (!stop.stop_requested() &&
             (result = av_read_frame(format, packet.value)) >= 0) {
        const auto index = packet.value->stream_index;
        auto* stream = format->streams[index];
        const auto time = Time(packet.value, stream) - base;
        if (index == video && packet.value->dts == AV_NOPTS_VALUE) {
          if (leading_dts == AV_NOPTS_VALUE)
            throw std::runtime_error("Clip has missing decode timestamps");
          packet.value->dts = leading_dts;
          leading_dts += step;
        }
        if (!started && index == video &&
            (packet.value->flags & AV_PKT_FLAG_KEY) && time >= cut - 0.001) {
          started = true;
          origin = time;
        }
        if (started && mapping[index] >= 0 && time >= origin - 0.001 &&
            time < cut_end) {
          // Cut video only at a complete GOP boundary. Including the final GOP
          // keeps B-frame references decodable.
          if (index == video) {
            ++frames;
            max_end = std::max(max_end, time + packet.value->duration *
                                                   av_q2d(stream->time_base));
          }
          const auto offset = static_cast<std::int64_t>(
              std::llround((origin + base) / av_q2d(stream->time_base)));
          if (packet.value->pts != AV_NOPTS_VALUE) packet.value->pts -= offset;
          if (packet.value->dts != AV_NOPTS_VALUE) packet.value->dts -= offset;
          auto* target = output.value->streams[mapping[index]];
          av_packet_rescale_ts(packet.value, stream->time_base,
                               target->time_base);
          packet.value->stream_index = mapping[index];
          packet.value->pos = -1;
          if (av_interleaved_write_frame(output.value, packet.value) < 0)
            throw std::runtime_error("Trimmed file write failed");
        }
        av_packet_unref(packet.value);
      }
      if (stop.stop_requested())
        throw std::runtime_error("Trim cancelled; original unchanged");
      if (result != AVERROR_EOF || !frames)
        throw std::runtime_error("Clip could not be trimmed completely");
      if (av_write_trailer(output.value) < 0 ||
          avio_closep(&output.value->pb) < 0)
        throw std::runtime_error("Cannot finalize trimmed copy");
      if (!MoveFileW(partial.c_str(), view.output.c_str()))
        throw std::runtime_error(
            "Cannot publish trimmed copy (file exists or folder unavailable)");
      partial.clear();
      view.actual_start = origin;
      view.actual_end = max_end;
      view.busy = false;
      view.message = "Trimmed copy saved. Original kept.";
      Publish(view);
    } catch (const std::exception& e) {
      std::error_code ignored;
      if (owns_partial && !partial.empty())
        std::filesystem::remove(partial, ignored);
      view.busy = false;
      view.output.clear();
      view.message = e.what();
      Publish(view);
    }
  });
  return true;
}
void ClipTrimmer::Cancel() {
  if (worker_.joinable()) worker_.request_stop();
}
void ClipTrimmer::Shutdown() {
  Cancel();
  if (worker_.joinable()) worker_.join();
  window_ = nullptr;
}
}  // namespace klip
