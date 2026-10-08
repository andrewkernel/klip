#pragma once

#include <chrono>

namespace klip::audio_detail {

inline bool ShouldRetryCapture(bool enabled, bool worker_failed, bool worker_joinable,
                               std::chrono::steady_clock::time_point now,
                               std::chrono::steady_clock::time_point retry_after) noexcept {
  return enabled && (worker_failed || !worker_joinable) && now >= retry_after;
}

}  // namespace klip::audio_detail
