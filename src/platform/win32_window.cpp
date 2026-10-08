#include "klip/platform/win32_window.h"

#include <dwmapi.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message,
                                                             WPARAM w_param, LPARAM l_param);

namespace klip {
namespace {

constexpr wchar_t kWindowClass[] = L"KlipWindowClass";

struct LogoPoint {
  float x;
  float y;
};

bool PointInPolygon(float x, float y, const std::array<LogoPoint, 4>& polygon) {
  bool inside = false;
  for (std::size_t current = 0, previous = polygon.size() - 1; current < polygon.size();
       previous = current++) {
    const auto& a = polygon[current];
    const auto& b = polygon[previous];
    if (((a.y > y) != (b.y > y)) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

bool PointInRoundedLeftBar(float x, float y) {
  constexpr float width = 11.0F;
  constexpr float height = 34.0F;
  constexpr float radius = 4.0F;
  if (x < 0.0F || x > width || y < 0.0F || y > height) return false;
  if (x >= radius && x <= width - radius) return true;
  if (y >= radius && y <= height - radius) return true;
  const float center_x = x < radius ? radius : width - radius;
  const float center_y = y < radius ? radius : height - radius;
  const float dx = x - center_x;
  const float dy = y - center_y;
  return dx * dx + dy * dy <= radius * radius;
}

HICON CreateKlipIcon(int size) {
  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = size;
  bitmap_info.bmiHeader.biHeight = -size;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  std::uint32_t* pixels = nullptr;
  HBITMAP color_bitmap = CreateDIBSection(nullptr, &bitmap_info, DIB_RGB_COLORS,
                                          reinterpret_cast<void**>(&pixels), nullptr, 0);
  if (color_bitmap == nullptr || pixels == nullptr) return nullptr;

  constexpr std::array<LogoPoint, 4> upper{
      {{8.0F, 15.0F}, {24.0F, 0.0F}, {35.0F, 0.0F}, {17.0F, 18.0F}}};
  constexpr std::array<LogoPoint, 4> lower{
      {{9.0F, 18.0F}, {18.0F, 10.0F}, {36.0F, 34.0F}, {23.0F, 34.0F}}};
  constexpr std::array<std::uint8_t, 3> left_color{139, 67, 247};
  constexpr std::array<std::uint8_t, 3> upper_color{177, 90, 255};
  constexpr std::array<std::uint8_t, 3> lower_color{103, 48, 219};
  constexpr int samples = 4;
  constexpr int sample_count = samples * samples;
  const float scale = static_cast<float>(size) * 0.82F / 36.0F;
  const float offset_x = (static_cast<float>(size) - 36.0F * scale) * 0.5F;
  const float offset_y = (static_cast<float>(size) - 34.0F * scale) * 0.5F;

  for (int pixel_y = 0; pixel_y < size; ++pixel_y) {
    for (int pixel_x = 0; pixel_x < size; ++pixel_x) {
      int red = 0;
      int green = 0;
      int blue = 0;
      int alpha = 0;
      for (int sample_y = 0; sample_y < samples; ++sample_y) {
        for (int sample_x = 0; sample_x < samples; ++sample_x) {
          const float x =
              (static_cast<float>(pixel_x) + (sample_x + 0.5F) / samples - offset_x) / scale;
          const float y =
              (static_cast<float>(pixel_y) + (sample_y + 0.5F) / samples - offset_y) / scale;
          const std::array<std::uint8_t, 3>* color = nullptr;
          if (PointInRoundedLeftBar(x, y)) color = &left_color;
          if (PointInPolygon(x, y, upper)) color = &upper_color;
          if (PointInPolygon(x, y, lower)) color = &lower_color;
          if (color != nullptr) {
            red += (*color)[0];
            green += (*color)[1];
            blue += (*color)[2];
            ++alpha;
          }
        }
      }
      const auto channel = [](int value) {
        return static_cast<std::uint32_t>(value / sample_count);
      };
      pixels[pixel_y * size + pixel_x] = (channel(alpha * 255) << 24U) | (channel(red) << 16U) |
                                         (channel(green) << 8U) | channel(blue);
    }
  }

  std::vector<std::uint8_t> mask(static_cast<std::size_t>(((size + 15) / 16) * 2 * size), 0);
  HBITMAP mask_bitmap = CreateBitmap(size, size, 1, 1, mask.data());
  ICONINFO icon_info{};
  icon_info.fIcon = TRUE;
  icon_info.hbmMask = mask_bitmap;
  icon_info.hbmColor = color_bitmap;
  HICON icon = mask_bitmap != nullptr ? CreateIconIndirect(&icon_info) : nullptr;
  if (mask_bitmap != nullptr) DeleteObject(mask_bitmap);
  DeleteObject(color_bitmap);
  return icon;
}

void ApplyDarkWindowChrome(HWND window) {
  constexpr DWORD kUseImmersiveDarkMode = 20;
  constexpr DWORD kBorderColor = 34;
  constexpr DWORD kCaptionColor = 35;
  constexpr DWORD kTextColor = 36;
  const BOOL dark = TRUE;
  const COLORREF border = RGB(39, 46, 57);
  const COLORREF caption = RGB(8, 12, 19);
  const COLORREF text = RGB(238, 240, 247);
  DwmSetWindowAttribute(window, kUseImmersiveDarkMode, &dark, sizeof(dark));
  DwmSetWindowAttribute(window, kBorderColor, &border, sizeof(border));
  DwmSetWindowAttribute(window, kCaptionColor, &caption, sizeof(caption));
  DwmSetWindowAttribute(window, kTextColor, &text, sizeof(text));
}

}  // namespace

Win32Window::~Win32Window() noexcept { Destroy(); }

bool Win32Window::Create(HINSTANCE instance, int show_command, Error& error) {
  instance_ = instance;
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_CLASSDC;
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  large_icon_ = CreateKlipIcon(64);
  small_icon_ = CreateKlipIcon(32);
  window_class.hIcon = large_icon_;
  window_class.hIconSm = small_icon_;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = kWindowClass;
  if (RegisterClassExW(&window_class) == 0) {
    if (small_icon_ != nullptr) DestroyIcon(small_icon_);
    if (large_icon_ != nullptr) DestroyIcon(large_icon_);
    small_icon_ = nullptr;
    large_icon_ = nullptr;
    error = MakeWin32Error(ErrorComponent::kWindow, "register window class", GetLastError());
    return false;
  }
  constexpr DWORD style =
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
  const int work_width = monitor.rcWork.right - monitor.rcWork.left;
  const int work_height = monitor.rcWork.bottom - monitor.rcWork.top;
  const int client_width = std::min(1260, std::max(960, work_width - 32));
  const int client_height = std::min(800, std::max(620, work_height - 48));
  RECT rectangle{0, 0, client_width, client_height};
  AdjustWindowRect(&rectangle, style, FALSE);
  const int width = rectangle.right - rectangle.left;
  const int height = rectangle.bottom - rectangle.top;
  const int x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
  const int y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
  window_ = CreateWindowW(kWindowClass, L"Klip", style, x, y, width, height, nullptr, nullptr,
                          instance, this);
  if (window_ == nullptr) {
    error = MakeWin32Error(ErrorComponent::kWindow, "create main window", GetLastError());
    UnregisterClassW(kWindowClass, instance_);
    return false;
  }
  ApplyDarkWindowChrome(window_);
  SendMessageW(window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon_));
  SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon_));
  visible_ = show_command != SW_HIDE;
  ShowWindow(window_, show_command);
  UpdateWindow(window_);
#if defined(KLIP_USE_LIBOBS)
  taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
  UpdateTray(true);
#endif
  return true;
}

void Win32Window::UpdateTray(bool add) noexcept {
#if defined(KLIP_USE_LIBOBS)
  NOTIFYICONDATAW icon{};
  icon.cbSize = sizeof(icon); icon.hWnd = window_; icon.uID = 1;
  icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  icon.uCallbackMessage = WM_APP + 54; icon.hIcon = small_icon_;
  wcscpy_s(icon.szTip, L"Klip / open or quit from the tray");
  if (add) tray_added_ = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
  else { Shell_NotifyIconW(NIM_DELETE, &icon); tray_added_ = false; }
#else
  (void)add;
#endif
}

void Win32Window::Destroy() noexcept {
  if (tray_added_) UpdateTray(false);
  if (window_ != nullptr) {
    if (IsWindow(window_)) {
      SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
      DestroyWindow(window_);
    }
    window_ = nullptr;
  }
  if (instance_ != nullptr) {
    UnregisterClassW(kWindowClass, instance_);
    instance_ = nullptr;
  }
  if (small_icon_ != nullptr) {
    DestroyIcon(small_icon_);
    small_icon_ = nullptr;
  }
  if (large_icon_ != nullptr) {
    DestroyIcon(large_icon_);
    large_icon_ = nullptr;
  }
}

bool Win32Window::PumpMessages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
    if (message.message == WM_QUIT) return false;
  }
  return true;
}

void Win32Window::ToggleVisibility() {
  visible_ = !visible_;
  ShowWindow(window_, visible_ ? SW_SHOW : SW_HIDE);
  if (visible_) SetForegroundWindow(window_);
}

void Win32Window::SetHotkeyCallback(HotkeyCallback callback) {
  hotkey_callback_ = std::move(callback);
}

bool Win32Window::TakeResize(UINT& width, UINT& height) {
  if (pending_width_ == 0 || pending_height_ == 0) return false;
  width = std::exchange(pending_width_, 0);
  height = std::exchange(pending_height_, 0);
  return true;
}

LRESULT CALLBACK Win32Window::WindowProc(HWND window, UINT message, WPARAM w_param,
                                         LPARAM l_param) {
  auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
    self = static_cast<Win32Window*>(create->lpCreateParams);
    self->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (self && message != WM_NCHITTEST) self->redraw_requested_ = true;
  if (ImGui::GetCurrentContext() != nullptr &&
      ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param))
    return 1;
  return self != nullptr ? self->HandleMessage(message, w_param, l_param)
                         : DefWindowProcW(window, message, w_param, l_param);
}

LRESULT Win32Window::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
#if defined(KLIP_USE_LIBOBS)
  if (taskbar_created_message_ && message == taskbar_created_message_) { UpdateTray(true); return 0; }
#endif
  switch (message) {
#if defined(KLIP_USE_LIBOBS)
    case WM_CLOSE:
      if (tray_added_) { if (visible_) ToggleVisibility(); return 0; }
      break;
    case WM_APP + 56:
      if (!visible_) ToggleVisibility();
      SetForegroundWindow(window_);
      return 0;
    case WM_APP + 54:
      if (l_param == WM_LBUTTONUP || l_param == WM_LBUTTONDBLCLK) {
        if (!visible_) ToggleVisibility();
        SetForegroundWindow(window_);
      } else if (l_param == WM_RBUTTONUP) {
        POINT position{}; GetCursorPos(&position);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Open Klip");
        AppendMenuW(menu, MF_STRING, 2, L"Quit Klip");
        SetForegroundWindow(window_);
        const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, position.x, position.y, 0, window_, nullptr);
        DestroyMenu(menu);
        if (command == 1) { if (!visible_) ToggleVisibility(); SetForegroundWindow(window_); }
        else if (command == 2) { UpdateTray(false); DestroyWindow(window_); }
      }
      return 0;
#endif
    case WM_GETMINMAXINFO: {
      auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
      RECT minimum{0, 0, 960, 620};
      AdjustWindowRect(
          &minimum,
          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX,
          FALSE);
      limits->ptMinTrackSize.x = minimum.right - minimum.left;
      limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
      return 0;
    }
    case WM_SIZE:
      minimized_ = w_param == SIZE_MINIMIZED;
      if (w_param != SIZE_MINIMIZED) {
        pending_width_ = LOWORD(l_param);
        pending_height_ = HIWORD(l_param);
      }
      return 0;
    case WM_HOTKEY:
      if (hotkey_callback_) hotkey_callback_(static_cast<int>(w_param));
      return 0;
    case WM_SYSCOMMAND:
      if ((w_param & 0xfff0) == SC_KEYMENU) return 0;
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window_, message, w_param, l_param);
}

}  // namespace klip
