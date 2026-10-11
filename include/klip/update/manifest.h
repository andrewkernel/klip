#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace klip {
struct UpdateManifest {
  std::string version, url, sha256, payload;
  std::uint64_t bytes = 0, expires = 0;
  std::vector<unsigned char> signature;
};
bool NewerVersion(std::string_view candidate, std::string_view current);
std::optional<UpdateManifest> ParseUpdateManifest(std::string_view text,
                                                  std::uint64_t now);
std::vector<unsigned char> DecodeHex(std::string_view text);
}  // namespace klip
