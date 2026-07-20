#pragma once

#include <optional>
#include <string>
#include <system_error>
#include <utility>

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

  Error() = default;
  Error(ErrorComponent component_value, std::string operation_value,
        std::string message_value, std::optional<long long> native_code_value = {},
        std::string native_description_value = {}, std::string context_value = {})
      : component(component_value),
        operation(std::move(operation_value)),
        message(std::move(message_value)),
        native_code(native_code_value),
        native_description(std::move(native_description_value)),
        context(std::move(context_value)) {}

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
