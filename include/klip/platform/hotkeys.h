#pragma once

#include <Windows.h>

#include "klip/core/config.h"
#include "klip/core/error.h"

namespace klip {

class Hotkeys {
 public:
  static constexpr int kSaveClip = 1;
  static constexpr int kToggleUi = 2;
  static constexpr int kToggleRecording = 3;

  Hotkeys() = default;
  ~Hotkeys() noexcept;
  Hotkeys(const Hotkeys&) = delete;
  Hotkeys& operator=(const Hotkeys&) = delete;

  bool Register(HWND window, const HotkeyConfig& config, Error& error);
  void Unregister() noexcept;
  [[nodiscard]] bool SaveRegistered() const noexcept { return save_registered_; }
  [[nodiscard]] bool ToggleRegistered() const noexcept { return toggle_registered_; }
  [[nodiscard]] bool RecordRegistered() const noexcept { return record_registered_; }

 private:
  HWND window_ = nullptr;
  HotkeyConfig config_;
  bool save_registered_ = false;
  bool toggle_registered_ = false;
  bool record_registered_ = false;
};

}  // namespace klip
