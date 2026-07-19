#include <Windows.h>
#include <imgui_impl_win32.h>
#include <winrt/base.h>

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

extern "C" {
#include <libavutil/log.h>
}

#include "klip/app/application.h"
#include "klip/core/config_store.h"
#include "klip/platform/user_paths.h"

namespace {

class ProcessApartment {
 public:
  ProcessApartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
  ~ProcessApartment() noexcept { winrt::uninit_apartment(); }
  ProcessApartment(const ProcessApartment&) = delete;
  ProcessApartment& operator=(const ProcessApartment&) = delete;
};

bool HasCommandLineFlag(PWSTR command_line, std::wstring_view flag) {
  if (command_line == nullptr || flag.empty()) return false;
  const std::wstring_view arguments(command_line);
  std::size_t position = 0;
  while ((position = arguments.find(flag, position)) != std::wstring_view::npos) {
    const bool starts_token = position == 0 || arguments[position - 1] == L' ' ||
                              arguments[position - 1] == L'\t';
    const auto end = position + flag.size();
    const bool ends_token = end == arguments.size() || arguments[end] == L' ' ||
                            arguments[end] == L'\t';
    if (starts_token && ends_token) return true;
    position = end;
  }
  return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int show_command) {
  try {
    const bool package_smoke_test =
        HasCommandLineFlag(command_line, L"--package-smoke-test");
    ProcessApartment apartment;
    av_log_set_level(AV_LOG_ERROR);
    ImGui_ImplWin32_EnableDpiAwareness();

    const auto user_paths = klip::ResolveUserPaths();
    klip::AppConfig config;
    klip::ApplyUserPathDefaults(config, user_paths);
    const bool settings_exist = std::filesystem::exists(user_paths.settings_file);
    std::string settings_diagnostic;
    if (!klip::LoadConfig(user_paths.settings_file, config, settings_diagnostic)) {
      MessageBoxA(nullptr, settings_diagnostic.c_str(), "Klip settings warning",
                  MB_OK | MB_ICONWARNING);
    } else if (!settings_exist &&
               !klip::SaveConfig(user_paths.settings_file, config, settings_diagnostic)) {
      MessageBoxA(nullptr, settings_diagnostic.c_str(), "Klip settings warning",
                  MB_OK | MB_ICONWARNING);
    }

    // The release workflow runs this from both the installed and portable packages on a clean
    // Windows runner. Reaching here proves the executable loader resolved every imported DLL and
    // that first-run path/config initialization works, without pretending a VM without gaming
    // hardware can validate WGC, WASAPI, or a vendor H.264 encoder.
    if (package_smoke_test) return 0;

    klip::KlipApplication application(std::move(config), user_paths.settings_file);
    klip::Error error;
    if (!application.Initialize(instance, show_command, error)) {
      MessageBoxA(nullptr, error.ToString().c_str(), "Klip startup failed", MB_OK | MB_ICONERROR);
      return 1;
    }
    return application.Run();
  } catch (const std::exception& exception) {
    MessageBoxA(nullptr, exception.what(), "Klip fatal error", MB_OK | MB_ICONERROR);
    return 1;
  } catch (...) {
    MessageBoxA(nullptr, "Unknown fatal error", "Klip fatal error", MB_OK | MB_ICONERROR);
    return 1;
  }
}
