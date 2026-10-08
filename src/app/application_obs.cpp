#include "klip/app/application.h"
#include "klip/core/config_store.h"
#include "klip/core/path_text.h"
#include "klip/ui/event_driven_tabs.h"

#include <shellapi.h>
#include <psapi.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
#include <fstream>

namespace klip {
namespace {
bool SendAcceptanceChord(unsigned modifiers, unsigned key) {
  std::vector<WORD> keys;
  if (modifiers & HotkeyConfig::kControl) keys.push_back(VK_CONTROL);
  if (modifiers & HotkeyConfig::kAlt) keys.push_back(VK_MENU);
  if (modifiers & HotkeyConfig::kShift) keys.push_back(VK_SHIFT);
  keys.push_back(static_cast<WORD>(key));
  std::vector<INPUT> events(keys.size() * 2);
  for (std::size_t i = 0; i < keys.size(); ++i) {
    events[i].type = INPUT_KEYBOARD; events[i].ki.wVk = keys[i];
    events[keys.size() + i].type = INPUT_KEYBOARD;
    events[keys.size() + i].ki.wVk = keys[keys.size() - i - 1];
    events[keys.size() + i].ki.dwFlags = KEYEVENTF_KEYUP;
  }
  if (SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT)) == events.size()) return true;
  SendInput(static_cast<UINT>(keys.size()), events.data() + keys.size(), sizeof(INPUT));
  return false;
}
std::size_t CountReplays(const std::filesystem::path& directory) {
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory))
    if (entry.is_regular_file() && entry.path().extension() == ".mkv") ++count;
  return count;
}
}
KlipApplication::KlipApplication(AppConfig config, std::filesystem::path settings_path)
    : config_(std::move(config)), settings_path_(std::move(settings_path)), engine_(state_, logger_) {}
KlipApplication::~KlipApplication() noexcept { Shutdown(); }

bool KlipApplication::Initialize(HINSTANCE instance, int show, Error& error, bool register_hotkeys,
    std::optional<std::uint32_t> vendor, std::optional<D3D_FEATURE_LEVEL> feature_level,
    bool, bool) {
  const auto issues = ValidateConfig(config_);
  if (!issues.empty()) { error = Error{ErrorComponent::kApplication, "validate settings", issues.front().field + ": " + issues.front().message}; return false; }
  std::error_code directory_error;
  std::filesystem::create_directories(config_.log_path.parent_path(), directory_error);
  if (directory_error || !logger_.Open(config_.log_path)) {
    error = Error{ErrorComponent::kApplication, "open log", "Could not open " + PathToUtf8(config_.log_path)};
    return false;
  }
  logger_.Info("Klip libobs startup");
  if (!window_.Create(instance, show, error) ||
      !graphics_.Initialize(window_.Handle(), error, vendor, feature_level) ||
      !imgui_.Initialize(window_.Handle(), graphics_.Device(), graphics_.Context(), error)) {
    logger_.ErrorMessage(error); Shutdown(); return false;
  }
  if (!StartMediaPipeline(config_, error)) {
    logger_.ErrorMessage(error);
    state_.SetError(error); // Keep settings usable so an unavailable encoder/device can be corrected.
  }
  const auto ui_feature_level = static_cast<unsigned>(graphics_.FeatureLevel());
  state_.SetGraphicsAdapter("UI adapter (OBS owns the capture/encoder device)", graphics_.AdapterVendorId(),
      std::to_string(ui_feature_level >> 12) + "." + std::to_string((ui_feature_level >> 8) & 15));
  if (register_hotkeys) {
    window_.SetHotkeyCallback([this](int id) { HandleHotkey(id); });
    Error hotkey_error;
    if (!hotkeys_.Register(window_.Handle(), config_.hotkeys, hotkey_error)) state_.SetError(hotkey_error);
  }
  initialized_ = true;
  return true;
}

int KlipApplication::Run() {
#if defined(KLIP_ENABLE_TEST_HOOKS)
  const auto benchmark_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(75);
  auto benchmark_ready = std::chrono::steady_clock::time_point{};
  bool benchmark_save = false;
  int benchmark_result = background_benchmark_ ? 2 : 0;
#endif
  const auto commands = BuildUiCommands();
  auto next_tick = std::chrono::steady_clock::now();
  auto next_render = next_tick;
  // ImGui trickles queued mouse-position/down/up events across frames, and tabs
  // schedule contents for a subsequent frame. Four frames drain a normal click
  // and update its contents without a continuously running idle render loop.
  unsigned pending_render_frames = kInputRedrawFrames;
  while (window_.PumpMessages()) {
    const auto now = std::chrono::steady_clock::now();
    engine_.SetDashboardActive(window_.DashboardActive());
    if (window_.TakeRedrawRequest()) pending_render_frames = std::max(pending_render_frames, kInputRedrawFrames);
    if (config_.obs_replay_enabled && now >= next_tick) {
      engine_.Tick(); next_tick = now + std::chrono::milliseconds(250);
#if defined(KLIP_ENABLE_TEST_HOOKS)
      if (background_benchmark_) {
        const auto snapshot = state_.Snapshot();
        if (snapshot.last_error || now > benchmark_deadline) {
          logger_.Warning("BACKGROUND BENCHMARK FAILED: capture error or deadline"); break;
        }
        if (benchmark_ready == std::chrono::steady_clock::time_point{} && snapshot.status == CaptureStatus::kBuffering) {
          benchmark_ready = now;
          logger_.Info("BACKGROUND BENCHMARK READY");
        }
        if (!benchmark_save && benchmark_ready != std::chrono::steady_clock::time_point{} &&
            now - benchmark_ready >= std::chrono::seconds(35)) {
          benchmark_save = engine_.SaveReplayClip();
          if (!benchmark_save) { logger_.Warning("BACKGROUND BENCHMARK FAILED: save rejected"); break; }
        }
        if (benchmark_save && !snapshot.last_saved_clip.empty()) {
          logger_.Info("BACKGROUND BENCHMARK PASSED: replay saved using normal event-driven loop");
          benchmark_result = 0; break;
        }
      }
#endif
      if (window_.DashboardActive()) pending_render_frames = std::max(pending_render_frames, 1U);
    }
    if (window_.DashboardActive() && pending_render_frames && now >= next_render) {
      UINT width = 0, height = 0;
      if (window_.TakeResize(width, height)) {
        Error error;
        if (!graphics_.Resize(width, height, error)) state_.SetError(error);
      }
      imgui_.BeginFrame();
      panel_.Render(state_.Snapshot(), config_, commands,
                    hotkeys_.SaveRegistered() && hotkeys_.RecordRegistered() && hotkeys_.ToggleRegistered(), {});
      constexpr float color[4]{0.06F, 0.07F, 0.09F, 1.0F};
      if (graphics_.BeginFrame(color)) { imgui_.Render(); graphics_.Present(); }
      --pending_render_frames;
      next_render = now + std::chrono::milliseconds(33);
    }
    DWORD timeout = config_.obs_replay_enabled ? 250 : INFINITE;
    if (window_.DashboardActive() && (pending_render_frames || ImGui::IsAnyItemActive())) {
      if (ImGui::IsAnyItemActive()) pending_render_frames = std::max(pending_render_frames, kInputRedrawFrames);
      timeout = 33;
    }
    MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }
  Shutdown();
#if defined(KLIP_ENABLE_TEST_HOOKS)
  if (background_benchmark_) return benchmark_result;
#endif
  return 0;
}

#if defined(KLIP_ENABLE_TEST_HOOKS)
int KlipApplication::RunBackgroundBenchmark() {
  background_benchmark_ = true;
  return Run();
}
#endif

void KlipApplication::Shutdown() noexcept {
  const bool initialized = initialized_.exchange(false);
  window_.SetHotkeyCallback({});
  hotkeys_.Unregister();
  StopMediaPipeline();
  imgui_.Shutdown();
  graphics_.Shutdown();
  window_.Destroy();
  if (initialized) logger_.Info("Klip libobs shutdown complete");
  logger_.Close();
}
bool KlipApplication::StartMediaPipeline(const AppConfig& config, Error& error) {
  engine_.SetDashboardActive(window_.DashboardActive());
  return engine_.Initialize(config, window_.Handle(), error);
}
void KlipApplication::StopMediaPipeline() noexcept { engine_.Shutdown(); }
bool KlipApplication::RestartMediaPipeline(const AppConfig& config, Error& error) { StopMediaPipeline(); return StartMediaPipeline(config, error); }
void KlipApplication::UpdateRollingMetrics() { engine_.Tick(); }
bool KlipApplication::StartRecording(Error& error) { return engine_.StartRecording(error); }

void KlipApplication::HandleHotkey(int id) {
  if (id == Hotkeys::kSaveClip) {
    if (!engine_.SaveReplayClip()) state_.SetError(Error{ErrorComponent::kClipWriter, "save replay", "OBS replay is not ready, or the save queue is full"});
    engine_.Tick();
  } else if (id == Hotkeys::kToggleRecording) {
    if (engine_.IsRecording()) engine_.StopRecording();
    else { Error error; if (!StartRecording(error)) state_.SetError(error); }
  } else if (id == Hotkeys::kToggleUi) window_.ToggleVisibility();
}

UiCommands KlipApplication::BuildUiCommands() {
  const auto audio = [this] { engine_.ConfigureAudio(config_); };
  return UiCommands{
      .save_clip = [this] { HandleHotkey(Hotkeys::kSaveClip); },
      .toggle_recording = [this] { HandleHotkey(Hotkeys::kToggleRecording); },
      .set_target_mode = [this](CaptureTargetMode mode) {
        Error error;
        engine_.RefreshSources();
        if (engine_.ConfigureCapture(mode, "", error)) { config_.target_mode = mode; PersistSettings(config_, false); }
        else state_.SetError(error);
      },
      .select_capture_source = [this](CaptureTargetMode mode, std::uint64_t, const std::string& label) {
        Error error;
        if (engine_.ConfigureCapture(mode, label, error)) {
          config_.target_mode = mode;
          if (mode == CaptureTargetMode::kDisplay) config_.preferred_display_name = label;
          else config_.preferred_game_title = label;
          PersistSettings(config_, false);
        } else state_.SetError(error);
      },
      .set_capture_border = [this](bool value) { config_.capture_border = value; PersistSettings(config_, false); },
      .set_capture_preview_enabled = [this](bool) { state_.SetSettingsStatus(false, "The libobs build does not render a capture preview."); },
      .set_desktop_audio_enabled = [this, audio](bool enabled) { config_.desktop_audio_enabled = enabled; audio(); PersistSettings(config_, false); },
      .set_microphone_enabled = [this, audio](bool enabled) { config_.microphone_enabled = enabled; audio(); PersistSettings(config_, false); },
      .set_desktop_audio_gain = [this, audio](float gain) { config_.desktop_audio_gain = std::clamp(static_cast<double>(gain), 0.0, 2.0); audio(); },
      .set_microphone_audio_gain = [this, audio](float gain) { config_.microphone_audio_gain = std::clamp(static_cast<double>(gain), 0.0, 2.0); audio(); },
      .persist_audio_gains = [this] { PersistSettings(config_, false); },
      .select_microphone = [this, audio](int index) {
        const auto snapshot = state_.Snapshot();
        if (index >= 0 && index < static_cast<int>(snapshot.microphones.size())) {
          config_.preferred_microphone_name = snapshot.microphones[static_cast<std::size_t>(index)];
          audio(); PersistSettings(config_, false);
        }
      },
      .save_settings = [this](const AppConfig& desired) { return ApplySettings(desired); },
      .open_output_folder = [this] { ShellExecuteW(nullptr, L"open", config_.output_directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL); },
      .open_file = [](const std::filesystem::path& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); },
      .refresh_capture_sources = [this] {engine_.RefreshSources();}};
}

bool KlipApplication::PersistSettings(const AppConfig& config, bool) {
  std::string message;
  if (!SaveConfig(settings_path_, config, message)) { state_.SetError(Error{ErrorComponent::kApplication, "save settings", message}); return false; }
  config_ = config;
  state_.SetSettingsStatus(false, "Settings saved and applied.");
  return true;
}

bool KlipApplication::ApplySettings(const AppConfig& desired) {
  const auto issues = ValidateConfig(desired);
  if (!issues.empty()) { state_.SetSettingsStatus(false, issues.front().field + ": " + issues.front().message); return false; }
  const auto snapshot = state_.Snapshot();
  if (snapshot.recording || snapshot.finalizing_recording || snapshot.status == CaptureStatus::kSaving) {
    state_.SetSettingsStatus(false, "Finish the recording or clip save before applying settings."); return false;
  }
  const auto previous = config_;
  const bool changed = previous.hotkeys != desired.hotkeys;
  Error error;
  if (changed && !hotkeys_.Register(window_.Handle(), desired.hotkeys, error)) {
    Error restore; hotkeys_.Register(window_.Handle(), previous.hotkeys, restore); state_.SetError(error); return false;
  }
  const bool restart = RequiresMediaPipelineReconfigure(previous, desired);
  if (restart && !RestartMediaPipeline(desired, error)) {
    Error restore;
    if (!RestartMediaPipeline(previous, restore)) logger_.ErrorMessage(restore);
    if (changed) hotkeys_.Register(window_.Handle(), previous.hotkeys, restore);
    state_.SetError(error); state_.SetSettingsStatus(false, "OBS rejected the settings; previous configuration restored when possible."); return false;
  }
  if (!PersistSettings(desired, false)) {
    Error restore;
    if (restart) RestartMediaPipeline(previous, restore);
    if (changed) hotkeys_.Register(window_.Handle(), previous.hotkeys, restore);
    return false;
  }
  engine_.ConfigureAudio(desired);
  if (snapshot.last_error && state_.Snapshot().error_generation == snapshot.error_generation)
    state_.ClearError(snapshot.last_error->component);
  return true;
}

int KlipApplication::RunCaptureAcceptanceTest(std::string required_encoder, std::uint32_t duration,
    bool source_switch, std::string, bool, bool actions, bool input) {
  if (source_switch) {
    logger_.Warning("This OBS slice has not implemented the legacy source-switch acceptance protocol.");
    Shutdown(); return 3;
  }
  if (state_.Snapshot().last_error) { logger_.Warning("OBS acceptance failed during initialization"); Shutdown(); return 2; }
  const auto dispatch_key = [&](unsigned modifiers, unsigned key, const auto& completed) {
    if (!SendAcceptanceChord(modifiers, key)) return false;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (window_.PumpMessages() && std::chrono::steady_clock::now() < end) {
      if (completed()) return true;
      MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    return false;
  };
  if (input) {
    const bool visible = window_.Visible();
    if (!dispatch_key(config_.hotkeys.toggle_ui_modifiers, config_.hotkeys.toggle_ui_virtual_key, [&] {return window_.Visible() != visible;}) ||
        !dispatch_key(config_.hotkeys.toggle_ui_modifiers, config_.hotkeys.toggle_ui_virtual_key, [&] {return window_.Visible() == visible;})) {
      logger_.Warning("OBS global show/hide hotkey test failed"); Shutdown(); return 3;
    }
    PostMessageW(window_.Handle(), WM_APP + 56, 0, 0);
    if (!window_.PumpMessages() || !window_.Visible()) { Shutdown(); return 3; }
    engine_.SetDashboardActive(window_.DashboardActive());
    ShowWindow(window_.Handle(), SW_MINIMIZE);
    window_.PumpMessages();
    engine_.SetDashboardActive(window_.DashboardActive());
    if (window_.DashboardActive()) { logger_.Warning("Minimized dashboard remained active"); Shutdown(); return 3; }
    ShowWindow(window_.Handle(), SW_RESTORE);
    window_.PumpMessages();
    engine_.SetDashboardActive(window_.DashboardActive());
    if (!window_.DashboardActive()) { logger_.Warning("Restored dashboard did not resume"); Shutdown(); return 3; }
    PostMessageW(window_.Handle(), WM_CLOSE, 0, 0);
    if (!window_.PumpMessages() || window_.Visible()) { logger_.Warning("Close-to-tray test failed"); Shutdown(); return 3; }
    engine_.SetDashboardActive(window_.DashboardActive());
    logger_.Info("OBS tray acceptance: dashboard closed; engine remains operational");
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration + 20);
  auto started = std::chrono::steady_clock::time_point{};
  bool recording_started = false, clip_requested = false, stop_requested = false;
  const auto original_errors = state_.Snapshot().error_generation;
  const auto clips_before = CountReplays(config_.output_directory);
  logger_.Info("OBS ACCEPTANCE STARTED");
  while (window_.PumpMessages() && std::chrono::steady_clock::now() < deadline) {
    engine_.SetDashboardActive(window_.DashboardActive());
    engine_.Tick();
    const auto now = std::chrono::steady_clock::now();
    const auto snapshot = state_.Snapshot();
    if (snapshot.error_generation != original_errors) break;
    if (!recording_started && snapshot.status == CaptureStatus::kBuffering) {
      Error error;
      if (input) recording_started = dispatch_key(config_.hotkeys.record_modifiers, config_.hotkeys.record_virtual_key, [&] {return engine_.IsRecording();});
      else if (actions) { PostMessageW(window_.Handle(), WM_HOTKEY, Hotkeys::kToggleRecording, 0); window_.PumpMessages(); recording_started = engine_.IsRecording(); }
      else recording_started = StartRecording(error);
      if (!recording_started) { logger_.ErrorMessage(error); break; }
      started = now;
    }
    if (recording_started && !clip_requested && now - started > std::chrono::seconds(duration / 2)) {
      clip_requested = true;
      for (int request = 0; request < 3; ++request) {
        if (input) {
          // SendInput delivery is asynchronous; pump the resulting WM_HOTKEY once per chord.
          if (!SendAcceptanceChord(config_.hotkeys.save_modifiers, config_.hotkeys.save_virtual_key)) clip_requested = false;
          MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
          window_.PumpMessages();
        } else if (actions) { PostMessageW(window_.Handle(), WM_HOTKEY, Hotkeys::kSaveClip, 0); window_.PumpMessages(); }
        else if (!engine_.SaveReplayClip()) clip_requested = false;
      }
    }
    if (recording_started && !stop_requested && now - started >= std::chrono::seconds(duration)) {
      if (input) {
        if (!dispatch_key(config_.hotkeys.record_modifiers, config_.hotkeys.record_virtual_key, [&] {return !engine_.IsRecording();})) break;
      } else engine_.StopRecording();
      stop_requested = true;
    }
    if (stop_requested && !snapshot.finalizing_recording && !snapshot.last_saved_recording.empty() && !snapshot.last_saved_clip.empty()) {
      if (required_encoder == "h264_nvenc") required_encoder = "obs_nvenc_h264_tex";
      if (!required_encoder.empty() && snapshot.selected_encoder != required_encoder) break;
      if (!std::filesystem::exists(snapshot.last_saved_clip) || !std::filesystem::exists(snapshot.last_saved_recording)) break;
      if (CountReplays(config_.output_directory) < clips_before + 3) continue;
      logger_.Info("OBS ACCEPTANCE PASSED encoder=" + snapshot.selected_encoder + " consecutive_saves=3 global_input=" + std::to_string(input) + " clip=" + PathToUtf8(snapshot.last_saved_clip) + " recording=" + PathToUtf8(snapshot.last_saved_recording));
      Shutdown(); return 0;
    }
    MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }
  logger_.Warning("OBS ACCEPTANCE FAILED or timed out; inspect media and preceding errors");
  Shutdown(); return 2;
}

int KlipApplication::RunSettingsAcceptanceTest() {
  auto updated = config_;
  updated.obs_cq = config_.obs_cq == 18 ? 20 : 18;
  if (!ApplySettings(updated)) { Shutdown(); return 5; }
  return RunCaptureAcceptanceTest();
}

int KlipApplication::RunSaveShutdownAcceptanceTest() {
  const auto errors_before = state_.Snapshot().error_generation;
  const auto clips_before = CountReplays(config_.output_directory);
  const auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (window_.PumpMessages() && std::chrono::steady_clock::now() < ready_deadline) {
    engine_.Tick();
    if (state_.Snapshot().status == CaptureStatus::kBuffering) break;
    MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }
  Error error;
  if (!engine_.StartRecording(error)) { logger_.ErrorMessage(error); Shutdown(); return 2; }
  const auto filled = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (window_.PumpMessages() && std::chrono::steady_clock::now() < filled) {
    engine_.Tick();
    MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }
  for (int i = 0; i < 3; ++i) {
    if (!engine_.SaveReplayClip()) { logger_.Warning("Shutdown test could not enqueue all three saves"); Shutdown(); return 2; }
  }
  // Intentionally do not Tick or StopRecording here. Engine shutdown must flush
  // accepted commands and finalize the shared-encoder recording itself.
  StopMediaPipeline();
  const auto snapshot = state_.Snapshot();
  if (snapshot.error_generation != errors_before || CountReplays(config_.output_directory) != clips_before + 3 ||
      snapshot.last_saved_recording.empty() || !std::filesystem::exists(snapshot.last_saved_recording)) {
    logger_.Warning("OBS QUEUED-SAVE SHUTDOWN ACCEPTANCE FAILED"); Shutdown(); return 2;
  }
  logger_.Info("OBS QUEUED-SAVE SHUTDOWN ACCEPTANCE PASSED consecutive_saves=3 recording_finalized=1");
  Shutdown();
  return 0;
}

int KlipApplication::RunSaveFailureRecoveryAcceptanceTest() {
  // Only an empty, isolated test destination directly under KLIP_DATA_ROOT may
  // be moved. Never move a user's populated clips folder or a computed broad root.
  const auto root = std::filesystem::weakly_canonical(config_.log_path.parent_path());
  const auto destination = std::filesystem::weakly_canonical(config_.output_directory);
  const auto away = root / "Clips-disconnected-test";
  if (destination.parent_path() != root || destination.filename() != "Clips" ||
      std::filesystem::exists(away) || !std::filesystem::is_empty(destination)) {
    logger_.Warning("Destination-loss acceptance requires an isolated, empty data-root/Clips folder");
    Shutdown(); return 3;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (window_.PumpMessages() && std::chrono::steady_clock::now() < deadline) {
    engine_.Tick();
    if (state_.Snapshot().status == CaptureStatus::kBuffering) break;
    MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }
  bool disconnected = false;
  bool verified = false;
  try {
    std::filesystem::rename(destination, away);
    disconnected = true;
    if (engine_.SaveReplayClip()) {
      engine_.Tick();
      const auto failure = state_.Snapshot();
      verified = failure.status == CaptureStatus::kFailed && failure.last_error.has_value() &&
                 failure.last_error->component == ErrorComponent::kClipWriter && !engine_.SaveReplayClip();
    }
    std::filesystem::rename(away, destination);
    disconnected = false;
  } catch (const std::exception& exception) {
    logger_.Warning(std::string("Destination-loss acceptance: ") + exception.what());
  }
  if (disconnected) {
    std::error_code restore_error;
    std::filesystem::rename(away, destination, restore_error);
    if (restore_error) logger_.Warning("Test folder restoration failed: " + restore_error.message());
  }
  if (!verified) { logger_.Warning("OBS destination-loss error handling failed"); Shutdown(); return 2; }
  const auto original = config_;
  auto disabled = original; disabled.obs_replay_enabled = false;
  if (!ApplySettings(disabled) || !ApplySettings(original) || state_.Snapshot().last_error) {
    logger_.Warning("OBS failed-save buffering restart did not recover"); Shutdown(); return 2;
  }
  logger_.Info("OBS DESTINATION LOSS RECOVERY PASSED error_visible=1 requests_blocked=1 settings_restart=1");
  // Verify real decoded media after the failed save and restart, not just status.
  return RunCaptureAcceptanceTest();
}

int KlipApplication::RunIdleAcceptanceTest() {
  if (config_.obs_replay_enabled || window_.Visible()) {
    logger_.Warning("Idle acceptance requires replay disabled and a hidden dashboard"); Shutdown(); return 3;
  }
  FILETIME created{}, exited{}, kernel_before{}, user_before{}, kernel_after{}, user_after{};
  const auto cpu = [](FILETIME time) { return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel_before, &user_before)) { Shutdown(); return 5; }
  const auto begin = std::chrono::steady_clock::now();
  while (window_.PumpMessages() && std::chrono::steady_clock::now() - begin < std::chrono::seconds(10))
    MsgWaitForMultipleObjectsEx(0, nullptr, 1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel_after, &user_after)) { Shutdown(); return 5; }
  const auto used = cpu(kernel_after) + cpu(user_after) - cpu(kernel_before) - cpu(user_before);
  const double percent = 100.0 * static_cast<double>(used) / 10'000'000.0 / seconds;
  PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
  if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) { Shutdown(); return 5; }
  std::ofstream report(config_.log_path.parent_path() / "idle-measurement.json");
  report << "{\"seconds\":" << seconds << ",\"cpu_percent_one_core\":" << percent
         << ",\"cpu_percent_machine\":" << percent / std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))
         << ",\"private_bytes\":" << memory.PrivateUsage << ",\"working_set_bytes\":" << memory.WorkingSetSize << "}";
  logger_.Info("OBS IDLE ACCEPTANCE measured CPU one-core-percent=" + std::to_string(percent) + " capture disabled; no video/audio engine started");
  Shutdown();
  return percent < 1.0 ? 0 : 2;
}
}  // namespace klip
