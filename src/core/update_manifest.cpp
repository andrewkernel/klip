#include <array>
#include <charconv>
#include <limits>

#include "klip/update/manifest.h"

namespace klip {
namespace {
template <class T>
bool Number(std::string_view text, T& value) {
  if (text.empty() || text.size() > 20 ||
      (text.size() > 1 && text.front() == '0'))
    return false;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool Version(std::string_view text, std::array<unsigned, 3>& values) {
  for (unsigned i = 0; i < 3; ++i) {
    const auto dot = text.find('.');
    if ((i < 2) != (dot != std::string_view::npos)) return false;
    if (!Number(text.substr(0, dot), values[i]) || values[i] > 65535)
      return false;
    if (i < 2) text.remove_prefix(dot + 1);
  }
  return true;
}
}  // namespace
std::vector<unsigned char> DecodeHex(std::string_view text) {
  if (text.empty() || text.size() % 2) return {};
  std::vector<unsigned char> bytes;
  const auto digit = [](char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
  };
  for (std::size_t i = 0; i < text.size(); i += 2) {
    const auto a = digit(text[i]), b = digit(text[i + 1]);
    if (a < 0 || b < 0) return {};
    bytes.push_back(static_cast<unsigned char>(a * 16 + b));
  }
  return bytes;
}
bool NewerVersion(std::string_view candidate, std::string_view current) {
  std::array<unsigned, 3> a{}, b{};
  return Version(candidate, a) && Version(current, b) && a > b;
}
std::optional<UpdateManifest> ParseUpdateManifest(std::string_view text,
                                                  std::uint64_t now) {
  if (text.size() > 4096 || text.empty() || text.back() != '\n') return {};
  std::array<std::string_view, 7> lines{};
  auto rest = text;
  for (auto& line : lines) {
    const auto end = rest.find('\n');
    if (end == std::string_view::npos) return {};
    line = rest.substr(0, end);
    rest.remove_prefix(end + 1);
  }
  std::array<unsigned, 3> version{};
  UpdateManifest result;
  if (!rest.empty() || lines[0] != "klip-update-v1" ||
      !Version(lines[1], version))
    return {};
  result.version = lines[1];
  result.url = lines[2];
  result.sha256 = lines[3];
  if (result.url != "https://klip-ten-zeta.vercel.app/Klip-" + result.version +
                        "-win64-setup.exe" ||
      DecodeHex(result.sha256).size() != 32 ||
      !Number(lines[4], result.bytes) || result.bytes < 65536 ||
      result.bytes > 256ULL * 1024 * 1024 ||
      !Number(lines[5], result.expires) || now > result.expires ||
      result.expires - now > 22ULL * 86400)
    return {};
  result.signature = DecodeHex(lines[6]);
  if (result.signature.size() != 64) return {};
  result.payload = text.substr(0, text.size() - lines[6].size() - 1);
  return result;
}
}  // namespace klip
