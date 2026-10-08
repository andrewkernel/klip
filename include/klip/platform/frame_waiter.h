#pragma once

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#include "klip/platform/scoped_handle.h"

namespace klip {

// A per-worker timer avoids the coarse Sleep tick without changing the system timer period.
class FrameWaiter {
 public:
  FrameWaiter()
      : timer_(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_MODIFY_STATE | SYNCHRONIZE)) {}

  void WaitUntil(std::chrono::steady_clock::time_point deadline) {
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) return;
    if (timer_.Get()) {
      LARGE_INTEGER due{};
      due.QuadPart = -std::max<std::int64_t>(
          1, std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100);
      if (SetWaitableTimer(timer_.Get(), &due, 0, nullptr, nullptr, FALSE) &&
          WaitForSingleObject(timer_.Get(), 100) == WAIT_OBJECT_0)
        return;
    }
    std::this_thread::sleep_until(deadline);
  }

 private:
  ScopedHandle timer_;
};

}  // namespace klip
