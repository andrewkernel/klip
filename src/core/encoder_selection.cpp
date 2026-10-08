#include "klip/core/encoder_selection.h"

#include <algorithm>
#include <string_view>

namespace klip {

std::vector<std::string> PrioritizeEncoders(const std::vector<std::string>& configured,
                                            std::uint32_t adapter_vendor_id) {
  auto prioritized = configured;
  const char* preferred = nullptr;
  if (adapter_vendor_id == 0x10DE) preferred = "h264_nvenc";
  if (adapter_vendor_id == 0x1002 || adapter_vendor_id == 0x1022) preferred = "h264_amf";
  if (adapter_vendor_id == 0x8086) preferred = "h264_mf";
  if (preferred == nullptr) return prioritized;
  const auto match = std::find(prioritized.begin(), prioritized.end(), preferred);
  if (match != prioritized.end()) std::rotate(prioritized.begin(), match, match + 1);
  return prioritized;
}

std::vector<std::string> FilterEncodersForAdapter(const std::vector<std::string>& configured,
                                                  std::uint32_t adapter_vendor_id) {
  if (adapter_vendor_id == 0) return configured;
  const auto belongs_to_other_vendor = [adapter_vendor_id](const std::string& encoder) {
    if (encoder == "h264_nvenc") return adapter_vendor_id != 0x10DE;
    if (encoder == "h264_amf")
      return adapter_vendor_id != 0x1002 && adapter_vendor_id != 0x1022;
    return false;
  };
  auto compatible = configured;
  compatible.erase(std::remove_if(compatible.begin(), compatible.end(), belongs_to_other_vendor),
                   compatible.end());
  return compatible;
}

std::string FormatEncoderSelectionFailure(const std::vector<std::string>& attempts) {
  constexpr std::size_t kAttemptDetailsLimit = 768;
  std::string message = "No H.264 encoder could be started.";
  if (!attempts.empty()) {
    message += " Encoder attempts: ";
    std::size_t details_size = 0;
    for (const auto& attempt : attempts) {
      const auto separator = details_size == 0 ? std::string_view{} : std::string_view{"; "};
      if (separator.size() + attempt.size() > kAttemptDetailsLimit - details_size) {
        message += "; additional encoder errors are in klip.log";
        break;
      }
      message.append(separator);
      message += attempt;
      details_size += separator.size() + attempt.size();
    }
  }
  message +=
      " Check the encoder error details; on Windows N, install the Media Feature Pack. "
      "Also verify the GPU driver and try a lower resolution or FPS.";
  return message;
}

}  // namespace klip
