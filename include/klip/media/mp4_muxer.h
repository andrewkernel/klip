#pragma once

#include <cstdint>
#include <filesystem>

#include "klip/core/error.h"
#include "klip/media/packet.h"

struct AVFormatContext;
struct AVStream;

namespace klip {

class Mp4Muxer {
 public:
  Mp4Muxer() = default;
  ~Mp4Muxer() noexcept;

  Mp4Muxer(const Mp4Muxer&) = delete;
  Mp4Muxer& operator=(const Mp4Muxer&) = delete;

  bool Open(const std::filesystem::path& output_path, const CodecSnapshot& video,
            const CodecSnapshot* audio, ErrorComponent component, Error& error);
  bool Write(const EncodedPacket& encoded, std::int64_t base_timestamp_100ns, Error& error);
  bool Finalize(Error& error);
  void Abort() noexcept;

 private:
  bool CloseFile(Error& error);

  AVFormatContext* format_ = nullptr;
  AVStream* video_stream_ = nullptr;
  AVStream* audio_stream_ = nullptr;
  std::filesystem::path output_path_;
  std::filesystem::path temporary_path_;
  ErrorComponent component_ = ErrorComponent::kApplication;
  std::int64_t last_video_dts_ = -1;
  std::int64_t last_audio_dts_ = -1;
  bool header_written_ = false;
  bool published_ = false;
};

}  // namespace klip
