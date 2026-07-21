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
    const bool capture_smoke_test =
        HasCommandLineFlag(command_line, L"--capture-smoke-test");
    const bool settings_apply_smoke_test =
        HasCommandLineFlag(command_line, L"--settings-apply-smoke-test");
    const bool software_fallback_smoke_test =
        HasCommandLineFlag(command_line, L"--software-fallback-smoke-test");
    const bool nvenc_stress_smoke_test =
        HasCommandLineFlag(command_line, L"--nvenc-stress-smoke-test");
    const bool high_frame_rate_smoke_test =
        HasCommandLineFlag(command_line, L"--120fps-smoke-test");
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

    if (capture_smoke_test || settings_apply_smoke_test || software_fallback_smoke_test ||
        nvenc_stress_smoke_test || high_frame_rate_smoke_test) {
      // Exercise the same conservative path on every tester. Vendor prioritization still chooses
      // NVENC, AMF, or Media Foundation from the actual adapter detected by D3D11.
      config.target_mode = klip::CaptureTargetMode::kDisplay;
      config.preferred_display_name.clear();
      config.preferred_game_title.clear();
      config.target_fps = 60;
      config.output_width = 1920;
      config.output_height = 1080;
      config.video_bitrate = 8'000'000;
      config.encoder_quality = klip::EncoderQuality::kPerformance;
      config.scaling_mode = klip::VideoScalingMode::kFit;
      config.encoder_preferences = {"h264_nvenc", "h264_amf", "h264_mf"};
      config.desktop_audio_enabled = true;
      config.excluded_audio_process.clear();
      config.microphone_enabled = false;
      config.capture_cursor = false;
      config.capture_border = false;
      config.capture_preview_enabled = false;
      config.static_overlay_enabled = false;
      config.live_overlay_enabled = false;
      if (software_fallback_smoke_test)
        config.encoder_preferences = {"h264_mf_software"};
      if (nvenc_stress_smoke_test) {
        config.output_width = 1280;
        config.output_height = 720;
        config.video_bitrate = 40'000'000;
        config.encoder_preferences = {"h264_nvenc"};
      }
      if (high_frame_rate_smoke_test) {
        config.target_fps = 120;
        config.output_width = 1280;
        config.output_height = 720;
        config.video_bitrate = 20'000'000;
        config.encoder_preferences = {"h264_nvenc"};
      }
    }

    // The release workflow runs this from both the installed and portable packages on a clean
    // Windows runner. Reaching here proves the executable loader resolved every imported DLL and
    // that first-run path/config initialization works, without pretending a VM without gaming
    // hardware can validate WGC, WASAPI, or a vendor H.264 encoder.
    if (package_smoke_test) return 0;

    klip::KlipApplication application(std::move(config), user_paths.settings_file);
    klip::Error error;
    const bool acceptance_test =
        capture_smoke_test || settings_apply_smoke_test || software_fallback_smoke_test ||
        nvenc_stress_smoke_test || high_frame_rate_smoke_test;
    if (!application.Initialize(instance, acceptance_test ? SW_HIDE : show_command, error,
                                !acceptance_test)) {
      MessageBoxA(nullptr, error.ToString().c_str(), "Klip startup failed", MB_OK | MB_ICONERROR);
      return 1;
    }
    if (settings_apply_smoke_test) return application.RunSettingsAcceptanceTest();
    if (nvenc_stress_smoke_test)
      return application.RunCaptureAcceptanceTest("h264_nvenc");
    if (high_frame_rate_smoke_test)
      return application.RunCaptureAcceptanceTest("h264_nvenc");
    return (capture_smoke_test || software_fallback_smoke_test)
               ? application.RunCaptureAcceptanceTest()
               : application.Run();
  } catch (const std::exception& exception) {
    MessageBoxA(nullptr, exception.what(), "Klip fatal error", MB_OK | MB_ICONERROR);
    return 1;
  } catch (...) {
    MessageBoxA(nullptr, "Unknown fatal error", "Klip fatal error", MB_OK | MB_ICONERROR);
    return 1;
  }
}
