#include <Windows.h>
#include <imgui_impl_win32.h>
#include <winrt/base.h>

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#if !defined(KLIP_USE_LIBOBS)
extern "C" {
#include <libavutil/log.h>
}
#endif

#include "klip/app/application.h"
#include "klip/core/config_store.h"
#include "klip/platform/user_paths.h"
#include "klip/platform/scoped_handle.h"

namespace {

class ProcessApartment {
 public:
  ProcessApartment() {
#if defined(KLIP_USE_LIBOBS)
    winrt::init_apartment(winrt::apartment_type::single_threaded);
#else
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif
  }
  ~ProcessApartment() noexcept { winrt::uninit_apartment(); }
  ProcessApartment(const ProcessApartment&) = delete;
  ProcessApartment& operator=(const ProcessApartment&) = delete;
};

bool HasCommandLineFlag(PWSTR command_line, std::wstring_view flag) {
  if (command_line == nullptr || flag.empty()) return false;
  const std::wstring_view arguments(command_line);
  std::size_t position = 0;
  while ((position = arguments.find(flag, position)) != std::wstring_view::npos) {
    const bool starts_token =
        position == 0 || arguments[position - 1] == L' ' || arguments[position - 1] == L'\t';
    const auto end = position + flag.size();
    const bool ends_token =
        end == arguments.size() || arguments[end] == L' ' || arguments[end] == L'\t';
    if (starts_token && ends_token) return true;
    position = end;
  }
  return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int show_command) {
  try {
    const bool package_smoke_test = HasCommandLineFlag(command_line, L"--package-smoke-test");
#if defined(KLIP_USE_LIBOBS) && defined(KLIP_ENABLE_TEST_HOOKS)
    const bool obs_background_benchmark = HasCommandLineFlag(command_line, L"--obs-background-benchmark");
    if (obs_background_benchmark && GetEnvironmentVariableW(L"KLIP_DATA_ROOT", nullptr, 0) == 0) return 2;
#else
    constexpr bool obs_background_benchmark = false;
    // Diagnostic control is compiled out of ordinary shipping binaries.
    // Reject it rather than starting a normal capture against user settings.
    if (HasCommandLineFlag(command_line, L"--obs-background-benchmark")) return 2;
#endif
#if defined(KLIP_USE_LIBOBS)
    const bool obs_idle_test = HasCommandLineFlag(command_line, L"--obs-idle-smoke-test");
    const bool obs_save_shutdown_test = HasCommandLineFlag(command_line, L"--obs-save-shutdown-smoke-test");
    const bool obs_save_failure_test = HasCommandLineFlag(command_line, L"--obs-save-failure-smoke-test");
#else
    constexpr bool obs_idle_test = false;
    constexpr bool obs_save_shutdown_test = false;
    constexpr bool obs_save_failure_test = false;
#endif
    const bool hotkey_actions_smoke_test =
        HasCommandLineFlag(command_line, L"--hotkey-actions-smoke-test");
    const bool hotkey_input_smoke_test =
        HasCommandLineFlag(command_line, L"--hotkey-input-smoke-test");
    const bool long_av_sync_smoke_test =
        HasCommandLineFlag(command_line, L"--long-av-sync-smoke-test");
    const bool configured_capture_smoke_test =
        HasCommandLineFlag(command_line, L"--configured-capture-smoke-test");
    const bool capture_smoke_test = configured_capture_smoke_test ||
                                   HasCommandLineFlag(command_line, L"--capture-smoke-test");
    const bool thirty_fps_smoke_test =
        HasCommandLineFlag(command_line, L"--30fps-smoke-test");
    const bool window_capture_smoke_test =
        HasCommandLineFlag(command_line, L"--window-capture-smoke-test");
    const bool window_120fps_smoke_test =
        HasCommandLineFlag(command_line, L"--window-120fps-smoke-test");
    const bool window_resize_smoke_test =
        HasCommandLineFlag(command_line, L"--window-resize-smoke-test");
    const bool source_switch_smoke_test =
        HasCommandLineFlag(command_line, L"--source-switch-smoke-test");
    const bool source_switch_av_smoke_test =
        HasCommandLineFlag(command_line, L"--source-switch-av-smoke-test");
    const bool source_sized_window_smoke_test =
        HasCommandLineFlag(command_line, L"--source-sized-window-smoke-test");
    const bool source_switch_acceptance_test =
        source_switch_smoke_test || source_switch_av_smoke_test;
    const bool settings_apply_smoke_test =
        HasCommandLineFlag(command_line, L"--settings-apply-smoke-test");
    const bool software_fallback_smoke_test =
        HasCommandLineFlag(command_line, L"--software-fallback-smoke-test");
    const bool encoder_fallback_smoke_test =
        HasCommandLineFlag(command_line, L"--encoder-fallback-smoke-test");
#if defined(KLIP_ENABLE_TEST_HOOKS)
    const bool runtime_encoder_fallback_smoke_test =
        HasCommandLineFlag(command_line, L"--runtime-encoder-fallback-smoke-test");
    const bool balanced_quality_smoke_test =
        HasCommandLineFlag(command_line, L"--balanced-quality-smoke-test");
#else
    constexpr bool runtime_encoder_fallback_smoke_test = false;
    constexpr bool balanced_quality_smoke_test = false;
#endif
    const bool software_performance_smoke_test =
        HasCommandLineFlag(command_line, L"--software-performance-smoke-test");
    const bool software_performance_120fps_smoke_test =
        HasCommandLineFlag(command_line, L"--software-performance-120fps-smoke-test");
    const bool video_only_smoke_test = HasCommandLineFlag(command_line, L"--video-only-smoke-test");
    const bool feature_level_10_smoke_test =
        HasCommandLineFlag(command_line, L"--feature-level-10-smoke-test");
    const bool event_query_sync_smoke_test =
        HasCommandLineFlag(command_line, L"--event-query-sync-smoke-test");
    const bool amd_hardware_smoke_test =
        HasCommandLineFlag(command_line, L"--amd-hardware-smoke-test");
    const bool amd_120fps_smoke_test = HasCommandLineFlag(command_line, L"--amd-120fps-smoke-test");
    const bool intel_hardware_smoke_test =
        HasCommandLineFlag(command_line, L"--intel-hardware-smoke-test");
    const bool nvenc_stress_smoke_test =
        HasCommandLineFlag(command_line, L"--nvenc-stress-smoke-test");
    const bool high_frame_rate_smoke_test =
        HasCommandLineFlag(command_line, L"--120fps-smoke-test");
    ProcessApartment apartment;
#if !defined(KLIP_USE_LIBOBS)
    av_log_set_level(AV_LOG_ERROR);
#endif
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

    if (!configured_capture_smoke_test && (hotkey_actions_smoke_test || hotkey_input_smoke_test || long_av_sync_smoke_test ||
        capture_smoke_test ||
        thirty_fps_smoke_test ||
        settings_apply_smoke_test || software_fallback_smoke_test ||
        encoder_fallback_smoke_test || runtime_encoder_fallback_smoke_test ||
        software_performance_smoke_test ||
        software_performance_120fps_smoke_test || video_only_smoke_test ||
        feature_level_10_smoke_test || window_capture_smoke_test || window_120fps_smoke_test ||
        event_query_sync_smoke_test || window_resize_smoke_test || source_switch_acceptance_test ||
        source_sized_window_smoke_test ||
        amd_hardware_smoke_test || amd_120fps_smoke_test || intel_hardware_smoke_test ||
        nvenc_stress_smoke_test || high_frame_rate_smoke_test)) {
      // Exercise the same conservative path on every tester. Vendor prioritization still chooses
      // NVENC, AMF, or Media Foundation from the actual adapter detected by D3D11.
      config.target_mode = klip::CaptureTargetMode::kDisplay;
      config.preferred_display_name.clear();
      config.preferred_game_title.clear();
      if (window_capture_smoke_test || window_120fps_smoke_test || window_resize_smoke_test ||
          source_switch_smoke_test || source_sized_window_smoke_test) {
        config.target_mode = klip::CaptureTargetMode::kGameWindow;
        config.preferred_game_title = "Klip cadence test scene  [cadence_scene.exe]";
      }
      if (window_resize_smoke_test) {
        config.output_width = 0;
        config.output_height = 0;
      }
      config.target_fps = 60;
      config.output_width = 1920;
      config.output_height = 1080;
      config.video_bitrate = 8'000'000;
      config.encoder_quality = klip::EncoderQuality::kPerformance;
      if (balanced_quality_smoke_test) config.encoder_quality = klip::EncoderQuality::kBalanced;
      config.scaling_mode = klip::VideoScalingMode::kFit;
      config.encoder_preferences = {"h264_nvenc", "h264_amf", "h264_mf"};
      if (hotkey_actions_smoke_test || hotkey_input_smoke_test) {
        config.hotkeys.save_modifiers = klip::HotkeyConfig::kControl | klip::HotkeyConfig::kAlt |
                                        klip::HotkeyConfig::kShift | klip::HotkeyConfig::kNoRepeat;
        config.hotkeys.save_virtual_key = VK_F22;
        config.hotkeys.record_modifiers = config.hotkeys.save_modifiers;
        config.hotkeys.record_virtual_key = VK_F23;
        config.hotkeys.toggle_ui_modifiers = config.hotkeys.save_modifiers;
        config.hotkeys.toggle_ui_virtual_key = VK_F24;
      }
      config.desktop_audio_enabled = true;
      if (video_only_smoke_test) config.desktop_audio_enabled = false;
      config.excluded_audio_process.clear();
      config.microphone_enabled = false;
      config.capture_cursor = false;
      config.capture_border = false;
      config.capture_preview_enabled = false;
      config.static_overlay_enabled = false;
      config.live_overlay_enabled = false;
      if (thirty_fps_smoke_test) {
        config.target_fps = 30;
        config.output_width = 1280;
        config.output_height = 720;
        config.video_bitrate = 6'000'000;
      }
      if (source_sized_window_smoke_test) {
        config.output_width = 0;
        config.output_height = 0;
      }
      if (software_fallback_smoke_test || software_performance_smoke_test)
        config.encoder_preferences = {"h264_mf_software"};
      if (encoder_fallback_smoke_test)
        config.encoder_preferences = {"h264_klip_test_missing", "h264_mf_software"};
      if (runtime_encoder_fallback_smoke_test)
        config.encoder_preferences = {"h264_nvenc", "h264_amf", "h264_mf"};
      if (software_performance_smoke_test) {
        config.output_width = 1280;
        config.output_height = 720;
        config.video_bitrate = 8'000'000;
      }
      if (software_performance_120fps_smoke_test) {
        config.target_fps = 120;
        config.output_width = 1280;
        config.output_height = 720;
        config.video_bitrate = 20'000'000;
        config.encoder_preferences = {"h264_mf_software"};
      }
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
      if (window_120fps_smoke_test) {
        config.target_fps = 120;
        config.output_width = 1280;
        config.output_height = 720;
        config.video_bitrate = 20'000'000;
        config.encoder_preferences = {"h264_nvenc"};
      }
      if (amd_hardware_smoke_test) config.encoder_preferences = {"h264_amf"};
      if (amd_120fps_smoke_test) {
        config.target_fps = 120;
        config.output_width = 1280;
        config.output_height = 720;
        config.video_bitrate = 20'000'000;
        config.encoder_preferences = {"h264_amf"};
      }
      if (intel_hardware_smoke_test) config.encoder_preferences = {"h264_mf"};
    }

    // The release workflow runs this from both the installed and portable packages on a clean
    // Windows runner. Reaching here proves the executable loader resolved every imported DLL and
    // that first-run path/config initialization works, without pretending a VM without gaming
    // hardware can validate WGC, WASAPI, or a vendor H.264 encoder.
    if (package_smoke_test) return 0;

    klip::KlipApplication application(std::move(config), user_paths.settings_file);
    klip::Error error;
    const bool acceptance_test =
        obs_background_benchmark || obs_idle_test || obs_save_shutdown_test || obs_save_failure_test || hotkey_actions_smoke_test || hotkey_input_smoke_test || long_av_sync_smoke_test ||
        capture_smoke_test ||
        thirty_fps_smoke_test ||
        settings_apply_smoke_test || software_fallback_smoke_test ||
        encoder_fallback_smoke_test || runtime_encoder_fallback_smoke_test ||
        video_only_smoke_test || software_performance_smoke_test ||
        software_performance_120fps_smoke_test ||
        feature_level_10_smoke_test || window_capture_smoke_test || window_120fps_smoke_test ||
        event_query_sync_smoke_test || window_resize_smoke_test || source_switch_acceptance_test ||
        source_sized_window_smoke_test ||
        amd_hardware_smoke_test || amd_120fps_smoke_test || intel_hardware_smoke_test ||
        nvenc_stress_smoke_test || high_frame_rate_smoke_test;
    const std::optional<std::uint32_t> requested_graphics_vendor =
        (amd_hardware_smoke_test || amd_120fps_smoke_test) ? std::optional<std::uint32_t>{0x1002}
        : intel_hardware_smoke_test                        ? std::optional<std::uint32_t>{0x8086}
                                                           : std::nullopt;
#if defined(KLIP_USE_LIBOBS)
    klip::ScopedHandle single_instance;
    if (!acceptance_test) {
      single_instance.Reset(CreateMutexW(nullptr, FALSE, L"Local\\Klip.Libobs.Engine"));
      if (single_instance.Get() && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"KlipWindowClass", L"Klip")) PostMessageW(existing, WM_APP + 56, 0, 0);
        return 0;
      }
    }
#endif
    const std::optional<D3D_FEATURE_LEVEL> requested_feature_level =
        feature_level_10_smoke_test ? std::optional<D3D_FEATURE_LEVEL>{D3D_FEATURE_LEVEL_10_0}
                                    : std::nullopt;
    if (!application.Initialize(instance, acceptance_test ? SW_HIDE : show_command, error,
                                !acceptance_test || hotkey_actions_smoke_test ||
                                    hotkey_input_smoke_test,
                                requested_graphics_vendor,
                                requested_feature_level, event_query_sync_smoke_test,
                                runtime_encoder_fallback_smoke_test)) {
      if (!acceptance_test)
        MessageBoxA(nullptr, error.ToString().c_str(), "Klip startup failed", MB_OK | MB_ICONERROR);
      return 1;
    }
    if (settings_apply_smoke_test) return application.RunSettingsAcceptanceTest();
#if defined(KLIP_USE_LIBOBS)
#if defined(KLIP_ENABLE_TEST_HOOKS)
    if (obs_background_benchmark) return application.RunBackgroundBenchmark();
#endif
    if (obs_idle_test) return application.RunIdleAcceptanceTest();
    if (obs_save_shutdown_test) return application.RunSaveShutdownAcceptanceTest();
    if (obs_save_failure_test) return application.RunSaveFailureRecoveryAcceptanceTest();
#endif
    if (hotkey_actions_smoke_test)
      return application.RunCaptureAcceptanceTest({}, 10, false, {}, false, true);
    if (hotkey_input_smoke_test)
      return application.RunCaptureAcceptanceTest({}, 10, false, {}, false, false, true);
    if (long_av_sync_smoke_test) return application.RunCaptureAcceptanceTest({}, 60);
    if (nvenc_stress_smoke_test) return application.RunCaptureAcceptanceTest("h264_nvenc", 60);
    if (high_frame_rate_smoke_test) return application.RunCaptureAcceptanceTest("h264_nvenc");
    if (source_switch_acceptance_test)
      return application.RunCaptureAcceptanceTest(
          amd_hardware_smoke_test ? "h264_amf" : "", source_switch_av_smoke_test ? 15 : 10,
          true, source_switch_av_smoke_test ? "Klip cadence switch source" : "");
    if (amd_hardware_smoke_test) return application.RunCaptureAcceptanceTest("h264_amf");
    if (amd_120fps_smoke_test) return application.RunCaptureAcceptanceTest("h264_amf");
    if (intel_hardware_smoke_test) return application.RunCaptureAcceptanceTest("h264_mf");
    return (capture_smoke_test || thirty_fps_smoke_test || software_fallback_smoke_test ||
            encoder_fallback_smoke_test || runtime_encoder_fallback_smoke_test ||
            software_performance_smoke_test || software_performance_120fps_smoke_test ||
            video_only_smoke_test || window_capture_smoke_test ||
            feature_level_10_smoke_test || event_query_sync_smoke_test ||
            window_120fps_smoke_test || window_resize_smoke_test || source_switch_acceptance_test ||
            source_sized_window_smoke_test)
               ? application.RunCaptureAcceptanceTest({}, 10, source_switch_acceptance_test, {},
                                                      runtime_encoder_fallback_smoke_test)
               : application.Run();
  } catch (const std::exception& exception) {
    MessageBoxA(nullptr, exception.what(), "Klip fatal error", MB_OK | MB_ICONERROR);
    return 1;
  } catch (...) {
    MessageBoxA(nullptr, "Unknown fatal error", "Klip fatal error", MB_OK | MB_ICONERROR);
    return 1;
  }
}
