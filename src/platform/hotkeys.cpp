#include "klip/platform/hotkeys.h"

namespace klip {

Hotkeys::~Hotkeys() noexcept { Unregister(); }

bool Hotkeys::Register(HWND window, const HotkeyConfig& config, Error& error) {
  Unregister();
  window_ = window;
  save_registered_ =
      RegisterHotKey(window, kSaveClip, config.save_modifiers, config.save_virtual_key) == TRUE;
  const DWORD save_error = save_registered_ ? ERROR_SUCCESS : GetLastError();
  record_registered_ =
      RegisterHotKey(window, kToggleRecording, config.record_modifiers,
                     config.record_virtual_key) == TRUE;
  const DWORD record_error = record_registered_ ? ERROR_SUCCESS : GetLastError();
  toggle_registered_ =
      RegisterHotKey(window, kToggleUi, config.toggle_ui_modifiers,
                     config.toggle_ui_virtual_key) == TRUE;
  const DWORD toggle_error = toggle_registered_ ? ERROR_SUCCESS : GetLastError();
  if (!save_registered_ || !record_registered_ || !toggle_registered_) {
    const auto detail = FormatHotkey(config.save_modifiers, config.save_virtual_key) + " / " +
                        FormatHotkey(config.record_modifiers, config.record_virtual_key) + " / " +
                        FormatHotkey(config.toggle_ui_modifiers,
                                     config.toggle_ui_virtual_key);
    error = MakeWin32Error(ErrorComponent::kHotkeys, "register global hotkeys",
                           !save_registered_ ? save_error
                                             : (!record_registered_ ? record_error : toggle_error),
                           detail);
    Unregister();
    return false;
  }
  return true;
}

void Hotkeys::Unregister() noexcept {
  if (window_ != nullptr && save_registered_) UnregisterHotKey(window_, kSaveClip);
  if (window_ != nullptr && record_registered_) UnregisterHotKey(window_, kToggleRecording);
  if (window_ != nullptr && toggle_registered_) UnregisterHotKey(window_, kToggleUi);
  save_registered_ = false;
  toggle_registered_ = false;
  record_registered_ = false;
  window_ = nullptr;
}

}  // namespace klip
