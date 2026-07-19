#pragma once

#include <Windows.h>

#include <functional>
#include <string>

#include "klip/core/error.h"

namespace klip {

class Win32Window {
 public:
  using HotkeyCallback = std::function<void(int)>;

  Win32Window() = default;
  ~Win32Window() noexcept;
  Win32Window(const Win32Window&) = delete;
  Win32Window& operator=(const Win32Window&) = delete;

  bool Create(HINSTANCE instance, int show_command, Error& error);
  void Destroy() noexcept;
  bool PumpMessages();
  void ToggleVisibility();
  void SetHotkeyCallback(HotkeyCallback callback);
  bool TakeResize(UINT& width, UINT& height);
  [[nodiscard]] HWND Handle() const noexcept { return window_; }
  [[nodiscard]] bool Visible() const noexcept { return visible_; }

 private:
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
  LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HICON large_icon_ = nullptr;
  HICON small_icon_ = nullptr;
  bool visible_ = true;
  UINT pending_width_ = 0;
  UINT pending_height_ = 0;
  HotkeyCallback hotkey_callback_;
};

}  // namespace klip
