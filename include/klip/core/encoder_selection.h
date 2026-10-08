#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace klip {

std::vector<std::string> PrioritizeEncoders(const std::vector<std::string>& configured,
                                            std::uint32_t adapter_vendor_id);
std::vector<std::string> FilterEncodersForAdapter(const std::vector<std::string>& configured,
                                                  std::uint32_t adapter_vendor_id);
std::string FormatEncoderSelectionFailure(const std::vector<std::string>& attempts);

}  // namespace klip
