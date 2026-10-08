#pragma once

namespace klip {
// A tray-hidden or minimized window has no dashboard work to display.
// Capture/audio/output operation is intentionally independent of this state.
constexpr bool DashboardWorkEnabled(bool shown, bool minimized) noexcept {
  return shown && !minimized;
}
}  // namespace klip
