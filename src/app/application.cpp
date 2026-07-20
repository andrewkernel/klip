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

bool KlipApplication::Initialize(HINSTANCE instance, int show_command, Error& error) {
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
      !video_encoder_.Initialize(graphics_.Device(), graphics_.Context(),
                                 graphics_.AdapterVendorId(), config_, error) ||
      !capture_.Initialize(graphics_.Device(), graphics_.Context(), window_.Handle(), config_,
                           error) ||
      !audio_.Initialize(capture_.QpcOrigin(), capture_.QpcFrequency(), config_, error) ||
      !clip_writer_.Start(
          config_,
          [this](CodecSnapshot& snapshot) { return video_encoder_.SnapshotCodec(snapshot); },
          [this](CodecSnapshot& snapshot) { return audio_encoder_.SnapshotCodec(snapshot); },
          error) ||
      !recording_writer_.Initialize(
          config_,
          [this](CodecSnapshot& snapshot) { return video_encoder_.SnapshotCodec(snapshot); },
          [this](CodecSnapshot& snapshot) { return audio_encoder_.SnapshotCodec(snapshot); },
          error) ||
      !capture_.Start(error)) {
    state_.SetError(error);
    logger_.ErrorMessage(error);
    Shutdown();
    return false;
  }

  logger_.Info("Graphics adapter: " + WideToUtf8(graphics_.AdapterName()));
  window_.SetHotkeyCallback([this](int id) { HandleHotkey(id); });
  Error hotkey_error;
  if (!hotkeys_.Register(window_.Handle(), config_.hotkeys, hotkey_error)) {
    logger_.Warning(hotkey_error.ToString());
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
    panel_.Render(state_.Snapshot(), config_, commands, hotkeys_available);
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

void KlipApplication::Shutdown() noexcept {
  const bool was_initialized = initialized_.exchange(false, std::memory_order_acq_rel);
  if (was_initialized) state_.SetStatus(CaptureStatus::kStopping, "Stopping Klip");
  window_.SetHotkeyCallback({});
  hotkeys_.Unregister();
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
            audio_.SetDesktopGain(static_cast<float>(updated.desktop_audio_gain));
            audio_.SetMicrophoneGain(static_cast<float>(updated.microphone_audio_gain));
            PersistSettings(updated, true);
          },
      .open_output_folder =
          [this] {
            const auto folder = config_.output_directory.wstring();
            ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          }};
}

bool KlipApplication::PersistSettings(const AppConfig& config, bool restart_required) {
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
  state_.SetSettingsStatus(restart_required,
                           restart_required ? "Saved. Restart Klip to apply capture changes."
                                            : "Settings saved.");
  logger_.Info("Settings saved: " + settings_path_.string());
  return true;
}

void KlipApplication::UpdateRollingMetrics() {
  const auto buffer = media_buffer_.Stats();
  state_.SetRollingMetrics(buffer.duration_seconds, buffer.bytes, buffer.packets);
  recording_writer_.Tick();
}

}  // namespace klip
