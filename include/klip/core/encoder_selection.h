#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace klip {

std::vector<std::string> PrioritizeEncoders(const std::vector<std::string>& configured,
                                            std::uint32_t adapter_vendor_id);

}  // namespace klip
