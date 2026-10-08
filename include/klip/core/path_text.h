#pragma once

#include <filesystem>
#include <string>

namespace klip {

inline std::string PathToUtf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}

}  // namespace klip
