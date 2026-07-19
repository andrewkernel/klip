#include "klip/core/encoder_selection.h"

#include <algorithm>

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

}  // namespace klip
