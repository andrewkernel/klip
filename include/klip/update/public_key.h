#pragma once
#include <string_view>
namespace klip {
// Public CNG P-256 blob. Private key is non-exportable in the publisher's
// Windows account.
inline constexpr std::string_view kUpdatePublicKey =
    "4543533120000000e84c56528a949cfe61240c627f45355ed66fad099576204189a26a222e"
    "9f7deaaad6a2f277ccb1e6d4f080fea804792c722cad403862cc9e8cb52238abe095bb";
}  // namespace klip
