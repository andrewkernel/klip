#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#if defined(KLIP_USE_LIBOBS)
#include "klip/obs/obs_engine.h"
#else
#include "klip/audio/audio_pipeline.h"
#include "klip/buffer/rolling_media_buffer.h"
#include "klip/capture/graphics_capture.h"
#include "klip/clips/clip_writer.h"
#include "klip/core/application_state.h"
#include "klip/core/config.h"
#include "klip/core/logger.h"
#include "klip/encoding/audio_encoder.h"
#include "klip/encoding/video_encoder.h"
#include "klip/media/packet_router.h"
#include "klip/recording/recording_writer.h"
#endif
#include "klip/core/application_state.h"
#include "klip/core/config.h"
#include "klip/core/logger.h"
#include "klip/graphics/d3d_device.h"
#include "klip/platform/hotkeys.h"
#include "klip/platform/win32_window.h"
#include "klip/ui/imgui_host.h"
#include "klip/ui/main_panel.h"

namespace klip {

class KlipApplication {
 public:
  KlipApplication(AppConfig config, std::filesystem::path settings_path);
  ~KlipApplication() noexcept;

  KlipApplication(const KlipApplication&) = delete;
  KlipApplication& operator=(const KlipApplication&) = delete;

  bool Initialize(HINSTANCE instance, int show_command, Error& error, bool register_hotkeys = true,
                  std::optional<std::uint32_t> graphics_vendor_id = std::nullopt,
                  std::optional<D3D_FEATURE_LEVEL> graphics_feature_level = std::nullopt,
                  bool force_event_query_sync = false,
                  bool inject_runtime_encoder_failure = false);
  int Run();
  int RunCaptureAcceptanceTest(std::string required_encoder = {},
                               std::uint32_t recording_duration_seconds = 10,
                               bool exercise_source_switch = false,
                               std::string alternate_window_title = {},
                               bool allow_encoder_recovery = false,
                               bool exercise_hotkey_actions = false,
                               bool exercise_global_hotkey_input = false);
  int RunSettingsAcceptanceTest();
  int RunIdleAcceptanceTest();
#if defined(KLIP_USE_LIBOBS)
  int RunSaveShutdownAcceptanceTest();
  int RunSaveFailureRecoveryAcceptanceTest();
#if defined(KLIP_ENABLE_TEST_HOOKS)
  int RunBackgroundBenchmark();
#endif
#endif
  void Shutdown() noexcept;

 private:
  void HandleHotkey(int id);
  UiCommands BuildUiCommands();
  void UpdateRollingMetrics();
  bool PersistSettings(const AppConfig& config, bool restart_required);
  bool ApplySettings(const AppConfig& config);
  bool StartRecording(Error& error);
  bool StartMediaPipeline(const AppConfig& config, Error& error);
  void StopMediaPipeline() noexcept;
  bool RestartMediaPipeline(const AppConfig& config, Error& error);

  AppConfig config_;
  std::filesystem::path settings_path_;
  ApplicationState state_;
  Logger logger_;
#if defined(KLIP_USE_LIBOBS)
  ObsEngine engine_;
#else
  RollingMediaBuffer media_buffer_;
  RecordingWriter recording_writer_;
  PacketRouter packet_router_;
  AudioEncoder audio_encoder_;
  VideoEncoder video_encoder_;
  GraphicsCapture capture_;
  AudioPipeline audio_;
  ClipWriter clip_writer_;
#endif
  Win32Window window_;
  D3dDevice graphics_;
  ImGuiHost imgui_;
  Hotkeys hotkeys_;
  MainPanel panel_;
  std::atomic<bool> initialized_{false};
#if defined(KLIP_USE_LIBOBS) && defined(KLIP_ENABLE_TEST_HOOKS)
  bool background_benchmark_ = false;
#endif
#if !defined(KLIP_USE_LIBOBS)
  bool force_event_query_sync_ = false;
#if defined(KLIP_ENABLE_TEST_HOOKS)
  bool inject_runtime_encoder_failure_ = false;
#endif
#endif
};

}  // namespace klip
