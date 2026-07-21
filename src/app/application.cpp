#include "klip/app/application.h"

#include "klip/core/config_store.h"

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

namespace klip {
namespace {

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return "Unknown";
  const auto required =
      WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (required <= 0) return "Unknown";
  std::string output(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), required, nullptr, nullptr);
  output.pop_back();
  return output;
}

bool SameHotkeys(const HotkeyConfig& left, const HotkeyConfig& right) {
  return left.save_modifiers == right.save_modifiers &&
         left.save_virtual_key == right.save_virtual_key &&
         left.record_modifiers == right.record_modifiers &&
         left.record_virtual_key == right.record_virtual_key &&
         left.toggle_ui_modifiers == right.toggle_ui_modifiers &&
         left.toggle_ui_virtual_key == right.toggle_ui_virtual_key;
}

}  // namespace

KlipApplication::KlipApplication(AppConfig config, std::filesystem::path settings_path)
    : config_(std::move(config)),
      settings_path_(std::move(settings_path)),
      media_buffer_(config_.rolling_buffer_seconds, config_.rolling_buffer_bytes),
      recording_writer_(state_, logger_),
      packet_router_(media_buffer_, recording_writer_),
      audio_encoder_(packet_router_, logger_),
      video_encoder_(packet_router_, state_, logger_),
      capture_(video_encoder_, state_, logger_),
      audio_(audio_encoder_, state_, logger_),
      clip_writer_(media_buffer_, state_, logger_, config_.clip_request_queue_capacity) {}

KlipApplication::~KlipApplication() noexcept { Shutdown(); }

bool KlipApplication::Initialize(HINSTANCE instance, int show_command, Error& error,
                                 bool register_hotkeys) {
  // Preview is the in-app replacement for the Windows capture highlight. Normalize older or
  // hand-edited settings so enabling the preview never leaves the yellow border visible too.
  if (config_.capture_preview_enabled) config_.capture_border = false;
  const auto issues = ValidateConfig(config_);
  if (!issues.empty()) {
    error = Error{ErrorComponent::kApplication, "validate configuration",
                  issues.front().field + " " + issues.front().message, {}, {}, {}};
    return false;
  }
  if (config_.output_directory.is_relative()) {
    config_.output_directory = std::filesystem::current_path() / config_.output_directory;
  }
  if (config_.recording_directory.is_relative()) {
    config_.recording_directory = std::filesystem::current_path() / config_.recording_directory;
  }
  if (config_.log_path.is_relative()) {
    config_.log_path = std::filesystem::current_path() / config_.log_path;
  }
  std::error_code directory_error;
  if (!config_.log_path.parent_path().empty())
    std::filesystem::create_directories(config_.log_path.parent_path(), directory_error);
  if (directory_error) {
    error = Error{ErrorComponent::kApplication, "create log directory",
                  directory_error.message(), {}, {}, config_.log_path.parent_path().string()};
    return false;
  }
  if (!logger_.Open(config_.log_path)) {
    error = Error{
        ErrorComponent::kApplication, "open log", "could not open the configured log file", {}, {},
        config_.log_path.string()};
    return false;
  }
  logger_.Info("Klip startup");
  logger_.Info(std::string("Build: ") + __DATE__ + " " + __TIME__);
  state_.SetStatus(CaptureStatus::kStarting, "Starting Klip");

  if (!window_.Create(instance, show_command, error) ||
      !graphics_.Initialize(window_.Handle(), error) ||
      !imgui_.Initialize(window_.Handle(), graphics_.Device(), graphics_.Context(), error) ||
      !StartMediaPipeline(config_, error)) {
    state_.SetError(error);
    logger_.ErrorMessage(error);
    Shutdown();
    return false;
  }

  logger_.Info("Graphics adapter: " + WideToUtf8(graphics_.AdapterName()));
  if (register_hotkeys) {
    window_.SetHotkeyCallback([this](int id) { HandleHotkey(id); });
    Error hotkey_error;
    if (!hotkeys_.Register(window_.Handle(), config_.hotkeys, hotkey_error)) {
      logger_.Warning(hotkey_error.ToString());
    }
  } else {
    logger_.Info("Global hotkeys skipped for capture acceptance mode");
  }
  initialized_.store(true, std::memory_order_release);
  return true;
}

int KlipApplication::Run() {
  if (!initialized_.load(std::memory_order_acquire)) return 1;
  const auto commands = BuildUiCommands();
  auto next_hotkey_retry = std::chrono::steady_clock::now();
  auto next_metrics_update = std::chrono::steady_clock::now();
  while (window_.PumpMessages()) {
    const auto ui_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(33);
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_metrics_update) {
      UpdateRollingMetrics();
      next_metrics_update = now + std::chrono::milliseconds(250);
    }
    bool hotkeys_available = hotkeys_.SaveRegistered() && hotkeys_.RecordRegistered() &&
                             hotkeys_.ToggleRegistered();
    if (!hotkeys_available && now >= next_hotkey_retry) {
      Error hotkey_error;
      hotkeys_available = hotkeys_.Register(window_.Handle(), config_.hotkeys, hotkey_error);
      if (hotkeys_available) logger_.Info("Global hotkeys registered");
      next_hotkey_retry = now + std::chrono::seconds(3);
    }
    if (!window_.Visible()) {
      std::this_thread::sleep_until(ui_deadline);
      continue;
    }
    UINT width = 0;
    UINT height = 0;
    if (window_.TakeResize(width, height)) {
      Error error;
      if (!graphics_.Resize(width, height, error)) {
        state_.SetError(error);
        logger_.ErrorMessage(error);
      }
    }
    imgui_.BeginFrame();
    const auto capture_preview = capture_.Preview();
    panel_.Render(state_.Snapshot(), config_, commands, hotkeys_available,
                  {capture_preview.texture.get(), capture_preview.overlay_texture.get(),
                   capture_preview.width,
                   capture_preview.height});
    constexpr float clear_color[4] = {0.06F, 0.07F, 0.09F, 1.0F};
    if (graphics_.BeginFrame(clear_color)) {
      imgui_.Render();
      graphics_.Present();
    }
    std::this_thread::sleep_until(ui_deadline);
  }
  Shutdown();
  return 0;
}

int KlipApplication::RunCaptureAcceptanceTest() {
  if (!initialized_.load(std::memory_order_acquire)) return 1;

  logger_.Info("CAPTURE ACCEPTANCE STARTED");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  auto stop_recording_at = std::chrono::steady_clock::time_point{};
  auto next_start_attempt = std::chrono::steady_clock::now();
  bool recording_started = false;
  std::string selected_encoder;

  while (window_.PumpMessages() && std::chrono::steady_clock::now() < deadline) {
    const auto now = std::chrono::steady_clock::now();
    UpdateRollingMetrics();
    const auto snapshot = state_.Snapshot();
    if (!snapshot.selected_encoder.empty()) selected_encoder = snapshot.selected_encoder;

    if (!recording_started && snapshot.status == CaptureStatus::kBuffering &&
        !snapshot.selected_encoder.empty() && now >= next_start_attempt) {
      Error start_error;
      if (recording_writer_.StartRecording(start_error)) {
        recording_started = true;
        stop_recording_at = now + std::chrono::seconds(5);
        logger_.Info("CAPTURE ACCEPTANCE RECORDING encoder=" + snapshot.selected_encoder);
      } else {
        // Codec snapshots can lag the first selected-encoder status by a frame. Retry during the
        // bounded warm-up period instead of treating that expected race as a test failure.
        next_start_attempt = now + std::chrono::milliseconds(250);
      }
    }

    if (recording_started && now >= stop_recording_at) {
      recording_writer_.StopRecording();
      const auto finished = state_.Snapshot();
      std::error_code file_error;
      const auto bytes = finished.last_saved_recording.empty()
                             ? 0ULL
                             : std::filesystem::file_size(finished.last_saved_recording,
                                                          file_error);
      if (!file_error && bytes >= 64ULL * 1024ULL) {
        logger_.Info("CAPTURE ACCEPTANCE PASSED encoder=" + selected_encoder +
                     " bytes=" + std::to_string(bytes) +
                     " file=" + finished.last_saved_recording.string());
        Shutdown();
        return 0;
      }

      Error error{ErrorComponent::kApplication,
                  "capture acceptance test",
                  file_error ? file_error.message()
                             : "recording did not produce a complete MP4 of at least 64 KiB",
                  file_error.value(),
                  file_error.message(),
                  finished.last_saved_recording.string()};
      state_.SetError(error);
      logger_.ErrorMessage(error);
      logger_.Warning("CAPTURE ACCEPTANCE FAILED encoder=" + selected_encoder);
      Shutdown();
      return 4;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  if (recording_writer_.IsRecording()) recording_writer_.StopRecording();
  const auto snapshot = state_.Snapshot();
  const auto detail = snapshot.last_error ? snapshot.last_error->ToString()
                                          : "encoder or capture source did not become ready";
  const Error timeout_error{ErrorComponent::kApplication, "capture acceptance test", detail,
                            {}, {}, selected_encoder};
  state_.SetError(timeout_error);
  logger_.ErrorMessage(timeout_error);
  logger_.Warning("CAPTURE ACCEPTANCE TIMED OUT encoder=" + selected_encoder);
  Shutdown();
  return 2;
}

int KlipApplication::RunSettingsAcceptanceTest() {
  if (!initialized_.load(std::memory_order_acquire)) return 1;
  logger_.Info("SETTINGS APPLY ACCEPTANCE STARTED");

  AppConfig updated = config_;
  updated.target_fps = 60;
  updated.video_bitrate = config_.video_bitrate == 6'000'000 ? 8'000'000 : 6'000'000;
  // Exercise the recording-quality path during acceptance so a release cannot pass while the
  // OBS-aligned B-frame, HQ tuning, or lookahead configuration is broken on real hardware.
  updated.encoder_quality = EncoderQuality::kBalanced;
  if (!ApplySettings(updated)) {
    const auto snapshot = state_.Snapshot();
    logger_.Warning("SETTINGS APPLY ACCEPTANCE FAILED " + snapshot.settings_message);
    Shutdown();
    return 5;
  }
  if (config_.target_fps != updated.target_fps ||
      config_.video_bitrate != updated.video_bitrate ||
      state_.Snapshot().settings_restart_required) {
    logger_.Warning("SETTINGS APPLY ACCEPTANCE FAILED live config did not update");
    Shutdown();
    return 6;
  }

  logger_.Info("SETTINGS APPLY ACCEPTANCE PASSED fps=" +
               std::to_string(config_.target_fps) + " bitrate=" +
               std::to_string(config_.video_bitrate));

  const auto clip_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  bool clip_requested = false;
  while (window_.PumpMessages() && std::chrono::steady_clock::now() < clip_deadline) {
    UpdateRollingMetrics();
    const auto snapshot = state_.Snapshot();
    if (!clip_requested && snapshot.status == CaptureStatus::kBuffering &&
        !snapshot.selected_encoder.empty() && snapshot.metrics.rolling_buffer_seconds >= 1.5) {
      clip_requested = clip_writer_.RequestClip();
      if (!clip_requested) break;
      logger_.Info("SETTINGS APPLY CLIP ACCEPTANCE REQUESTED");
    }
    if (clip_requested && !snapshot.last_saved_clip.empty()) {
      std::error_code file_error;
      const auto bytes = std::filesystem::file_size(snapshot.last_saved_clip, file_error);
      if (!file_error && bytes >= 64ULL * 1024ULL) {
        logger_.Info("SETTINGS APPLY CLIP ACCEPTANCE PASSED bytes=" +
                     std::to_string(bytes) + " file=" + snapshot.last_saved_clip.string());
        return RunCaptureAcceptanceTest();
      }
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  const Error clip_error{ErrorComponent::kApplication,
                         "settings apply clip acceptance",
                         clip_requested ? "clip did not produce a complete MP4 of at least 64 KiB"
                                        : "clip request was not accepted after pipeline refresh"};
  state_.SetError(clip_error);
  logger_.ErrorMessage(clip_error);
  Shutdown();
  return 7;
}

void KlipApplication::Shutdown() noexcept {
  const bool was_initialized = initialized_.exchange(false, std::memory_order_acq_rel);
  if (was_initialized) state_.SetStatus(CaptureStatus::kStopping, "Stopping Klip");
  window_.SetHotkeyCallback({});
  hotkeys_.Unregister();
  StopMediaPipeline();
  imgui_.Shutdown();
  graphics_.Shutdown();
  window_.Destroy();
  if (was_initialized) logger_.Info("Klip shutdown complete");
  logger_.Close();
}

void KlipApplication::HandleHotkey(int id) {
  if (id == Hotkeys::kSaveClip) {
    clip_writer_.RequestClip();
  } else if (id == Hotkeys::kToggleRecording) {
    Error error;
    if (recording_writer_.IsRecording()) {
      recording_writer_.StopRecording();
    } else if (state_.Snapshot().status != CaptureStatus::kBuffering) {
      return;
    } else if (!recording_writer_.StartRecording(error)) {
      state_.SetError(error);
      logger_.ErrorMessage(error);
    }
  } else if (id == Hotkeys::kToggleUi) {
    window_.ToggleVisibility();
  }
}

UiCommands KlipApplication::BuildUiCommands() {
  return UiCommands{
      .save_clip = [this] { clip_writer_.RequestClip(); },
      .toggle_recording =
          [this] {
            Error error;
            if (recording_writer_.IsRecording()) {
              recording_writer_.StopRecording();
            } else if (state_.Snapshot().status != CaptureStatus::kBuffering) {
              return;
            } else if (!recording_writer_.StartRecording(error)) {
              state_.SetError(error);
              logger_.ErrorMessage(error);
            }
          },
      .set_target_mode =
          [this](CaptureTargetMode mode) {
            packet_router_.ResetTimeline();
            config_.target_mode = mode;
            capture_.SetTargetMode(mode);
            PersistSettings(config_, false);
          },
      .select_capture_source =
          [this](CaptureTargetMode mode, std::uint64_t id, const std::string& label) {
            packet_router_.ResetTimeline();
            config_.target_mode = mode;
            if (mode == CaptureTargetMode::kDisplay)
              config_.preferred_display_name = label;
            else
              config_.preferred_game_title = label;
            capture_.SelectTarget(mode, id);
            PersistSettings(config_, false);
          },
      .set_capture_border =
          [this](bool required) {
            config_.capture_border = required;
            capture_.SetBorderRequired(required);
            PersistSettings(config_, false);
          },
      .set_capture_preview_enabled =
          [this](bool enabled) {
            config_.capture_preview_enabled = enabled;
            capture_.SetPreviewEnabled(enabled);
            if (enabled && config_.capture_border) {
              config_.capture_border = false;
              capture_.SetBorderRequired(false);
            }
            PersistSettings(config_, false);
          },
      .set_desktop_audio_enabled =
          [this](bool enabled) {
            config_.desktop_audio_enabled = enabled;
            audio_.SetDesktopEnabled(enabled);
            PersistSettings(config_, false);
          },
      .set_microphone_enabled =
          [this](bool enabled) {
            config_.microphone_enabled = enabled;
            audio_.SetMicrophoneEnabled(enabled);
            PersistSettings(config_, false);
          },
      .set_desktop_audio_gain =
          [this](float gain) {
            config_.desktop_audio_gain = std::clamp(static_cast<double>(gain), 0.0, 2.0);
            audio_.SetDesktopGain(gain);
          },
      .set_microphone_audio_gain =
          [this](float gain) {
            config_.microphone_audio_gain = std::clamp(static_cast<double>(gain), 0.0, 2.0);
            audio_.SetMicrophoneGain(gain);
          },
      .persist_audio_gains = [this] { PersistSettings(config_, false); },
      .select_microphone =
          [this](int index) {
            const auto snapshot = state_.Snapshot();
            if (index >= 0 && index < static_cast<int>(snapshot.microphones.size()))
              config_.preferred_microphone_name =
                  snapshot.microphones[static_cast<std::size_t>(index)];
            audio_.SelectMicrophone(index);
            PersistSettings(config_, false);
          },
      .save_settings =
          [this](const AppConfig& updated) {
            return ApplySettings(updated);
          },
      .open_output_folder =
          [this] {
            const auto folder = config_.output_directory.wstring();
            ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          }};
}

bool KlipApplication::ApplySettings(const AppConfig& updated) {
  AppConfig desired = updated;
  if (desired.capture_preview_enabled) desired.capture_border = false;
  const auto issues = ValidateConfig(desired);
  if (!issues.empty()) {
    const auto message = "Invalid setting: " + issues.front().field + " " +
                         issues.front().message;
    const Error error{ErrorComponent::kApplication, "apply settings", message};
    state_.SetError(error);
    state_.SetSettingsStatus(false, message);
    logger_.Warning(error.ToString());
    return false;
  }

  const AppConfig previous = config_;
  const auto snapshot = state_.Snapshot();
  if (snapshot.recording || snapshot.finalizing_recording ||
      snapshot.status == CaptureStatus::kSaving) {
    state_.SetSettingsStatus(false, "Finish the current clip or recording before applying settings.");
    return false;
  }

  const bool hotkeys_changed = !SameHotkeys(previous.hotkeys, desired.hotkeys);
  if (hotkeys_changed) {
    Error hotkey_error;
    if (!hotkeys_.Register(window_.Handle(), desired.hotkeys, hotkey_error)) {
      Error restore_error;
      hotkeys_.Register(window_.Handle(), previous.hotkeys, restore_error);
      const auto message = "Shortcut unavailable. Choose a different combination.";
      state_.SetError(hotkey_error);
      state_.SetSettingsStatus(false, message);
      logger_.Warning(hotkey_error.ToString());
      return false;
    }
  }

  const bool reconfigure_pipeline = RequiresMediaPipelineReconfigure(previous, desired);
  if (reconfigure_pipeline) {
    state_.SetStatus(CaptureStatus::kStarting, "Applying settings");
    Error apply_error;
    if (!RestartMediaPipeline(desired, apply_error)) {
      Error restore_error;
      const bool restored = RestartMediaPipeline(previous, restore_error);
      if (hotkeys_changed) {
        Error hotkey_restore_error;
        hotkeys_.Register(window_.Handle(), previous.hotkeys, hotkey_restore_error);
      }
      if (!restored) {
        apply_error = Error{ErrorComponent::kApplication,
                            "restore previous settings",
                            apply_error.ToString() + "; recovery failed: " +
                                restore_error.ToString()};
      }
      state_.SetError(apply_error);
      state_.SetSettingsStatus(
          false, restored ? "Could not apply that configuration. Previous settings restored."
                          : "Could not apply settings or restore capture. See klip.log.");
      logger_.ErrorMessage(apply_error);
      return false;
    }
  }

  if (!PersistSettings(desired, false)) {
    if (reconfigure_pipeline) {
      Error restore_error;
      if (!RestartMediaPipeline(previous, restore_error)) logger_.ErrorMessage(restore_error);
    }
    if (hotkeys_changed) {
      Error restore_error;
      hotkeys_.Register(window_.Handle(), previous.hotkeys, restore_error);
    }
    return false;
  }
  audio_.SetDesktopGain(static_cast<float>(desired.desktop_audio_gain));
  audio_.SetDesktopEnabled(desired.desktop_audio_enabled);
  if (previous.excluded_audio_process != desired.excluded_audio_process)
    audio_.SetExcludedApplication(desired.excluded_audio_process);
  audio_.SetMicrophoneGain(static_cast<float>(desired.microphone_audio_gain));
  audio_.SetMicrophoneEnabled(desired.microphone_enabled);
  capture_.SetPreviewEnabled(desired.capture_preview_enabled);
  capture_.SetBorderRequired(desired.capture_border);
  if (hotkeys_changed) logger_.Info("Global hotkeys updated");
  state_.ClearError();
  return true;
}

bool KlipApplication::PersistSettings(const AppConfig& config, bool /*restart_required*/) {
  std::string diagnostic;
  if (!SaveConfig(settings_path_, config, diagnostic)) {
    const Error error{ErrorComponent::kApplication, "save settings", diagnostic, {}, {},
                      settings_path_.string()};
    state_.SetError(error);
    state_.SetSettingsStatus(false, diagnostic);
    logger_.ErrorMessage(error);
    return false;
  }
  config_ = config;
  std::error_code ignored;
  std::filesystem::create_directories(config_.output_directory, ignored);
  std::filesystem::create_directories(config_.recording_directory, ignored);
  state_.SetSettingsStatus(false, "Settings saved and applied.");
  logger_.Info("Settings saved: " + settings_path_.string());
  return true;
}

bool KlipApplication::StartMediaPipeline(const AppConfig& config, Error& error) {
  media_buffer_.Reconfigure(config.rolling_buffer_seconds, config.rolling_buffer_bytes);
  if (!video_encoder_.Initialize(graphics_.Device(), graphics_.Context(),
                                 graphics_.AdapterVendorId(), config, error) ||
      !capture_.Initialize(graphics_.Device(), graphics_.Context(), window_.Handle(), config,
                           error) ||
      !audio_.Initialize(capture_.QpcOrigin(), capture_.QpcFrequency(), config, error) ||
      !clip_writer_.Start(
          config,
          [this](CodecSnapshot& snapshot) { return video_encoder_.SnapshotCodec(snapshot); },
          [this](CodecSnapshot& snapshot) { return audio_encoder_.SnapshotCodec(snapshot); },
          error) ||
      !recording_writer_.Initialize(
          config,
          [this](CodecSnapshot& snapshot) { return video_encoder_.SnapshotCodec(snapshot); },
          [this](CodecSnapshot& snapshot) { return audio_encoder_.SnapshotCodec(snapshot); },
          error) ||
      !capture_.Start(error)) {
    StopMediaPipeline();
    return false;
  }
  return true;
}

void KlipApplication::StopMediaPipeline() noexcept {
  capture_.Stop();
  audio_.StopCapture();
  video_encoder_.Flush();
  audio_encoder_.Flush();
  recording_writer_.StopRecording();
  clip_writer_.Stop();
  recording_writer_.Shutdown();
  audio_.Shutdown();
  video_encoder_.Shutdown();
  media_buffer_.Clear();
  packet_router_.ResetTimeline();
}

bool KlipApplication::RestartMediaPipeline(const AppConfig& config, Error& error) {
  StopMediaPipeline();
  return StartMediaPipeline(config, error);
}

void KlipApplication::UpdateRollingMetrics() {
  const auto buffer = media_buffer_.Stats();
  state_.SetRollingMetrics(buffer.duration_seconds, buffer.bytes, buffer.packets);
  recording_writer_.Tick();
}

}  // namespace klip
