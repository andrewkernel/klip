#pragma once

#include <Windows.h>

#include <atomic>
#include <filesystem>

#include "klip/audio/audio_pipeline.h"
#include "klip/buffer/rolling_media_buffer.h"
#include "klip/capture/graphics_capture.h"
#include "klip/clips/clip_writer.h"
#include "klip/core/application_state.h"
#include "klip/core/config.h"
#include "klip/core/logger.h"
#include "klip/encoding/audio_encoder.h"
#include "klip/encoding/video_encoder.h"
#include "klip/graphics/d3d_device.h"
#include "klip/media/packet_router.h"
#include "klip/platform/hotkeys.h"
#include "klip/recording/recording_writer.h"
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

  bool Initialize(HINSTANCE instance, int show_command, Error& error,
                  bool register_hotkeys = true);
  int Run();
  int RunCaptureAcceptanceTest();
  int RunSettingsAcceptanceTest();
  void Shutdown() noexcept;

 private:
  void HandleHotkey(int id);
  UiCommands BuildUiCommands();
  void UpdateRollingMetrics();
  bool PersistSettings(const AppConfig& config, bool restart_required);
  bool ApplySettings(const AppConfig& config);
  bool StartMediaPipeline(const AppConfig& config, Error& error);
  void StopMediaPipeline() noexcept;
  bool RestartMediaPipeline(const AppConfig& config, Error& error);

  AppConfig config_;
  std::filesystem::path settings_path_;
  ApplicationState state_;
  Logger logger_;
  RollingMediaBuffer media_buffer_;
  RecordingWriter recording_writer_;
  PacketRouter packet_router_;
  AudioEncoder audio_encoder_;
  VideoEncoder video_encoder_;
  GraphicsCapture capture_;
  AudioPipeline audio_;
  ClipWriter clip_writer_;
  Win32Window window_;
  D3dDevice graphics_;
  ImGuiHost imgui_;
  Hotkeys hotkeys_;
  MainPanel panel_;
  std::atomic<bool> initialized_{false};
};

}  // namespace klip
