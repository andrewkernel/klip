#include "klip/core/error.h"

#include <Windows.h>

#include <array>
#include <sstream>

#if !defined(KLIP_USE_LIBOBS)
extern "C" {
#include <libavutil/error.h>
}
#endif

namespace klip {
namespace {

std::string Win32Message(unsigned long code) {
  char* message = nullptr;
  const auto length = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<char*>(&message), 0, nullptr);
  std::string result =
      length != 0 && message != nullptr ? std::string(message, length) : "Unknown Windows error";
  if (message != nullptr) {
    LocalFree(message);
  }
  while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
    result.pop_back();
  }
  return result;
}

}  // namespace

std::string ComponentName(ErrorComponent component) {
  switch (component) {
    case ErrorComponent::kApplication:
      return "application";
    case ErrorComponent::kWindow:
      return "window";
    case ErrorComponent::kGraphics:
      return "graphics";
    case ErrorComponent::kCapture:
      return "capture";
    case ErrorComponent::kAudio:
      return "audio";
    case ErrorComponent::kVideoEncoder:
      return "video-encoder";
    case ErrorComponent::kAudioEncoder:
      return "audio-encoder";
    case ErrorComponent::kRollingBuffer:
      return "rolling-buffer";
    case ErrorComponent::kClipWriter:
      return "clip-writer";
    case ErrorComponent::kRecordingWriter:
      return "recording-writer";
    case ErrorComponent::kHotkeys:
      return "hotkeys";
  }
  return "unknown";
}

std::string Error::ToString() const {
  std::ostringstream stream;
  stream << '[' << ComponentName(component) << "] " << operation << ": " << message;
  if (native_code.has_value()) {
    stream << " (" << *native_code;
    if (!native_description.empty()) {
      stream << ": " << native_description;
    }
    stream << ')';
  }
  if (!context.empty()) {
    stream << " [" << context << ']';
  }
  return stream.str();
}

Error MakeWin32Error(ErrorComponent component, std::string operation, unsigned long code,
                     std::string context) {
  return Error{component,
               std::move(operation),
               "Windows operation failed",
               static_cast<long long>(code),
               Win32Message(code),
               std::move(context)};
}

Error MakeHresultError(ErrorComponent component, std::string operation, long result,
                       std::string context) {
  const auto code = static_cast<unsigned long>(result);
  return Error{component,
               std::move(operation),
               "COM operation failed",
               static_cast<long long>(result),
               Win32Message(code),
               std::move(context)};
}

Error MakeFfmpegError(ErrorComponent component, std::string operation, int code,
                      std::string context) {
#if !defined(KLIP_USE_LIBOBS)
  std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
  av_strerror(code, text.data(), text.size());
  return Error{component, std::move(operation), "FFmpeg operation failed",
               code,      text.data(),          std::move(context)};
#else
  return Error{component, std::move(operation), "FFmpeg operation failed", code, {},
               std::move(context)};
#endif
}

}  // namespace klip
