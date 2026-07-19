#pragma once

#include <optional>
#include <string>
#include <system_error>

namespace klip {

enum class ErrorComponent {
  kApplication,
  kWindow,
  kGraphics,
  kCapture,
  kAudio,
  kVideoEncoder,
  kAudioEncoder,
  kRollingBuffer,
  kClipWriter,
  kRecordingWriter,
  kHotkeys,
};

struct Error {
  ErrorComponent component = ErrorComponent::kApplication;
  std::string operation;
  std::string message;
  std::optional<long long> native_code;
  std::string native_description;
  std::string context;

  [[nodiscard]] std::string ToString() const;
};

std::string ComponentName(ErrorComponent component);
Error MakeWin32Error(ErrorComponent component, std::string operation, unsigned long code,
                     std::string context = {});
Error MakeHresultError(ErrorComponent component, std::string operation, long result,
                       std::string context = {});
Error MakeFfmpegError(ErrorComponent component, std::string operation, int code,
                      std::string context = {});

}  // namespace klip
