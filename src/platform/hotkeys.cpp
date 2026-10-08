#include "klip/platform/hotkeys.h"

namespace klip {

Hotkeys::~Hotkeys() noexcept { Unregister(); }

bool Hotkeys::Register(HWND window, const HotkeyConfig& config, Error& error) {
  if (window_ != window || !(config_ == config)) {
    Unregister();
    window_ = window;
    config_ = config;
  }
  save_registered_ = save_registered_ || RegisterHotKey(window, kSaveClip, config.save_modifiers,
                                                        config.save_virtual_key) == TRUE;
  const DWORD save_error = save_registered_ ? ERROR_SUCCESS : GetLastError();
  record_registered_ =
      record_registered_ || RegisterHotKey(window, kToggleRecording, config.record_modifiers,
                                           config.record_virtual_key) == TRUE;
  const DWORD record_error = record_registered_ ? ERROR_SUCCESS : GetLastError();
  toggle_registered_ =
      toggle_registered_ || RegisterHotKey(window, kToggleUi, config.toggle_ui_modifiers,
                                           config.toggle_ui_virtual_key) == TRUE;
  const DWORD toggle_error = toggle_registered_ ? ERROR_SUCCESS : GetLastError();
  if (!save_registered_ || !record_registered_ || !toggle_registered_) {
    std::string detail = "Unavailable shortcuts: ";
    if (!save_registered_)
      detail += "save clip (" + FormatHotkey(config.save_modifiers, config.save_virtual_key) + ") ";
    if (!record_registered_)
      detail +=
          "record (" + FormatHotkey(config.record_modifiers, config.record_virtual_key) + ") ";
    if (!toggle_registered_)
      detail += "show/hide (" +
                FormatHotkey(config.toggle_ui_modifiers, config.toggle_ui_virtual_key) + ") ";
    detail += "; other registered shortcuts remain available";
    error = MakeWin32Error(
        ErrorComponent::kHotkeys, "register global hotkeys",
        !save_registered_ ? save_error : (!record_registered_ ? record_error : toggle_error),
        detail);
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
