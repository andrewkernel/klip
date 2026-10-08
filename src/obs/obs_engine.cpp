#include "klip/obs/obs_engine.h"

#include <obs.h>
#include <obs-module.h>
#include <obs-audio-controls.h>
#include <util/base.h>
#include <util/platform.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>

#include "klip/core/path_text.h"
#include "klip/obs/encoder_policy.h"

namespace klip {
namespace {
using Data = std::unique_ptr<obs_data_t, decltype(&obs_data_release)>;
using Properties = std::unique_ptr<obs_properties_t, decltype(&obs_properties_destroy)>;
Data MakeData() { return Data(obs_data_create(), obs_data_release); }

std::filesystem::path RuntimeRoot() {
  std::wstring path(32768, L'\0');
  auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!size || size >= path.size()) throw std::runtime_error("Could not locate Klip runtime");
  path.resize(size);
  return std::filesystem::path(path).parent_path().parent_path().parent_path();
}

void ObsLog(int level, const char* format, va_list args, void* parameter) noexcept {
  if (level > LOG_INFO) return;
  std::array<char, 8192> text{};
  vsnprintf(text.data(), text.size(), format, args);
  auto& logger = *static_cast<Logger*>(parameter);
  try {
    if (level <= LOG_WARNING) logger.Warning(text.data());
    else logger.Info(text.data());
  } catch (...) {
    // Never unwind a C++ exception into libobs or its module threads.
    OutputDebugStringA(text.data());
  }
}

bool SetList(obs_properties_t* props, obs_data_t* settings, const char* key,
             const char* desired, bool required = true) {
  auto* property = obs_properties_get(props, key);
  if (property && obs_property_get_type(property) == OBS_PROPERTY_LIST) {
    for (std::size_t i = 0; i < obs_property_list_item_count(property); ++i) {
      if (!obs_property_list_item_disabled(property, i) &&
          std::string_view(obs_property_list_item_string(property, i)) == desired) {
        obs_data_set_string(settings, key, desired);
        return true;
      }
    }
  }
  if (required) throw std::runtime_error(std::string("Encoder does not support ") + key + "=" + desired);
  return false;
}

void SetInt(obs_properties_t* props, obs_data_t* settings, const char* key, int value) {
  auto* p = obs_properties_get(props, key);
  if (!p) return;
  if (obs_property_get_type(p) != OBS_PROPERTY_INT || value < obs_property_int_min(p) ||
      value > obs_property_int_max(p))
    throw std::runtime_error(std::string("Encoder property is out of range: ") + key);
  obs_data_set_int(settings, key, value);
}

bool SetListInt(obs_properties_t* props, obs_data_t* settings, const char* key, int value) {
  auto* property = obs_properties_get(props, key);
  if (!property || obs_property_get_type(property) != OBS_PROPERTY_LIST) return false;
  for (std::size_t i = 0; i < obs_property_list_item_count(property); ++i) {
    if (!obs_property_list_item_disabled(property, i) && obs_property_list_item_int(property, i) == value) {
      obs_data_set_int(settings, key, value); return true;
    }
  }
  return false;
}

std::string OutputError(obs_output_t* output) {
  const char* message = output ? obs_output_get_last_error(output) : nullptr;
  return message && *message ? message : "OBS output could not start; see the preceding OBS log details";
}

std::uint64_t ChoiceId(const std::string& value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : value) hash = (hash ^ c) * 1099511628211ULL;
  return hash;
}

bool ReserveOutputFilename(const std::filesystem::path& directory, const std::string& pattern,
                           OutputReservation& reservation, std::string& format, Error& error,
                           ErrorComponent component) {
  char* generated = os_generate_formatted_filename("mkv", false, pattern.c_str());
  if (!generated) { error = Error{component, "format output filename", "OBS filename generation failed"}; return false; }
  const auto* utf8 = reinterpret_cast<const char8_t*>(generated);
  const auto stem = std::filesystem::path(std::u8string(utf8)).stem();
  bfree(generated);
  for (std::size_t suffix = 0; suffix < 1000; ++suffix) {
    format = PathToUtf8(stem) + (suffix ? "_" + std::to_string(suffix + 1) : "");
    const auto* begin = reinterpret_cast<const char8_t*>(format.data());
    const auto path = directory / std::filesystem::path(std::u8string(begin, begin + format.size()) + u8".mkv");
    DWORD code = ERROR_SUCCESS;
    if (reservation.Reserve(path, code)) return true;
    if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS) {
      error = MakeWin32Error(component, "reserve output filename", code, PathToUtf8(path));
      return false;
    }
  }
  error = Error{component, "reserve output filename", "Too many files share the selected filename pattern"};
  return false;
}
}  // namespace

bool ObsEngine::Initialize(const AppConfig& config, HWND window, Error& error) {
  Shutdown();
  config_ = config;
  window_ = window;
  shutting_down_ = false;
  replay_stop_code_ = 0;
  replay_saved_ = false;
  recording_stop_event_ = 0;
  save_failed_ = false;
  state_.SetCaptureAdapter({}, {});
  state_.SetCaptureMetrics(0, 0, 0, 0, 0, 0, 0, 0);
  state_.SetRollingMetrics(0, 0, 0);
  if (!config.obs_replay_enabled) {
    state_.SetEncoder("disabled");
    state_.SetEncoderStatus("Replay buffering disabled; no encoder is running");
    state_.SetAudio(false, false, 0, 0);
    state_.SetStatus(CaptureStatus::kIdle, "Replay buffering disabled");
    return true;
  }
  try {
    if (!config.excluded_audio_process.empty()) {
      throw std::runtime_error("Stock OBS desktop audio cannot exclude one application's process tree. "
          "Clear the previous excluded-app setting under Audio to record all desktop audio; this change is not applied silently.");
    }
    std::filesystem::create_directories(config.output_directory);
    std::filesystem::create_directories(config.recording_directory);
    const auto root = RuntimeRoot();
    base_set_log_handler(ObsLog, &logger_);
    if (!obs_startup("en-US", PathToUtf8(config.log_path.parent_path()).c_str(), nullptr))
      throw std::runtime_error("obs_startup failed");
    initialized_ = true;
    if (std::string_view(obs_get_version_string()) != "32.1.2")
      throw std::runtime_error("Packaged libobs version differs from pinned 32.1.2");
    obs_hotkey_enable_background_press(false);  // Klip uses RegisterHotKey, not OBS polling.
    core_data_path_ = PathToUtf8(root / "data/libobs") + "/";
    obs_add_data_path(core_data_path_.c_str());

    obs_audio_info audio{};
    audio.samples_per_sec = 48000;
    audio.speakers = SPEAKERS_STEREO;
    if (!obs_reset_audio(&audio)) throw std::runtime_error("OBS 48 kHz stereo audio initialization failed");
    obs_video_info video{};
    const auto graphics_path = PathToUtf8(root / "bin/64bit/libobs-d3d11.dll");
    video.graphics_module = graphics_path.c_str();
    video.fps_num = config.target_fps;
    video.fps_den = 1;
    video.base_width = video.output_width = config.output_width ? config.output_width : 1920;
    video.base_height = video.output_height = config.output_height ? config.output_height : 1080;
    video.output_format = VIDEO_FORMAT_NV12;
    video.colorspace = VIDEO_CS_709;
    video.range = VIDEO_RANGE_PARTIAL;
    video.gpu_conversion = true;
    video.scale_type = OBS_SCALE_BICUBIC;
    if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS)
      throw std::runtime_error("OBS D3D11 video initialization failed");
    state_.SetCaptureAdapter("OBS D3D11 video engine", "Initialized / adapter details are in the OBS log");
    obs_enter_graphics();
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM parameter) -> BOOL {
      auto& logger = *reinterpret_cast<Logger*>(parameter);
      MONITORINFOEXA info{}; info.cbSize = sizeof(info);
      GetMonitorInfoA(monitor, &info);
      logger.Info(std::string("OBS DXGI monitor ") + info.szDevice + " index=" + std::to_string(gs_duplicator_get_monitor_index(monitor)));
      return TRUE;
    }, reinterpret_cast<LPARAM>(&logger_));
    obs_leave_graphics();

    std::vector<const char*> modules{"win-capture", "win-wasapi", "obs-ffmpeg", "obs-nvenc", "obs-qsv11", "obs-x264"};
    if (config.static_overlay_enabled) modules.push_back("image-source");
    if ((config.static_overlay_enabled || config.live_overlay_enabled) && config.static_overlay_opacity < 1.0)
      modules.push_back("obs-filters");
    for (const char* name : modules) {
      obs_module_t* module = nullptr;
      const auto binary = PathToUtf8(root / "obs-plugins/64bit" / (std::string(name) + ".dll"));
      const auto data = PathToUtf8(root / "data/obs-plugins" / name);
      const bool loaded = obs_open_module(&module, binary.c_str(), data.c_str()) == MODULE_SUCCESS &&
                          obs_init_module(module);
      logger_.Info(std::string("OBS module ") + name + (loaded ? " loaded" : " unavailable"));
      if (!loaded && (std::string_view(name) == "win-capture" ||
                      std::string_view(name) == "win-wasapi" || std::string_view(name) == "obs-ffmpeg" ||
                      std::string_view(name) == "image-source" || std::string_view(name) == "obs-filters"))
        throw std::runtime_error(std::string("Required OBS module failed: ") + name);
    }
    obs_post_load_modules();
    scene_ = obs_scene_create_private("Klip capture graph");
    if (!scene_) throw std::runtime_error("Could not create internal OBS scene");
    obs_set_output_source(0, obs_scene_get_source(scene_));
    RefreshSources();
    if (!ConfigureCapture(config.target_mode,
                          config.target_mode == CaptureTargetMode::kDisplay ? config.preferred_display_name : config.preferred_game_title,
                          error)) { Shutdown(); return false; }
    if (!ConfigureOverlay(error)) { Shutdown(); return false; }

    native_resolution_pending_ = !config.output_width || !config.output_height;
    ConfigureAudio(config);
    if (!ConfigureEncoder(error) || !StartReplayBuffer(error)) { Shutdown(); return false; }
    logger_.Info("LIBOBS ENGINE READY version=" + std::string(obs_get_version_string()) +
                 " resolution=" + std::to_string(video.output_width) + "x" + std::to_string(video.output_height) +
                 " fps=" + std::to_string(config.target_fps) + " NV12 Rec709 limited");
    Tick();
    return true;
  } catch (const std::exception& exception) {
    error = Error{ErrorComponent::kApplication, "initialize libobs", exception.what()};
    Shutdown();
    return false;
  }
}

void ObsEngine::RefreshSources() {
  if (!initialized_) return;
  const auto read = [](const char* source, const char* key, std::vector<SourceChoice>& choices) {
    choices.clear();
    Properties properties(obs_get_source_properties(source), obs_properties_destroy);
    if (!properties) return;
    auto* list = obs_properties_get(properties.get(), key);
    if (!list) return;
    for (std::size_t i = 0; i < obs_property_list_item_count(list); ++i) {
      if (obs_property_list_item_disabled(list, i)) continue;
      const std::string value = obs_property_list_item_string(list, i);
      if (value.empty()) continue;
      choices.push_back({ChoiceId(value), obs_property_list_item_name(list, i), value});
    }
  };
  read("game_capture", "window", games_);
  read("monitor_capture", "monitor_id", displays_);
  read("wasapi_input_capture", "device_id", microphones_);
  read("window_capture", "window", overlay_windows_);
  std::vector<CaptureSourceOption> games, displays;
  for (const auto& c : games_) games.push_back({c.id, c.label});
  for (const auto& c : displays_) displays.push_back({c.id, c.label});
  state_.SetCaptureSources(std::move(games), std::move(displays), selected_source_id_);
  std::vector<CaptureSourceOption> overlays;
  for (const auto& c : overlay_windows_) overlays.push_back({c.id, c.label});
  state_.SetOverlaySources(std::move(overlays));
  std::vector<std::string> microphones;
  int selected = -1;
  for (const auto& c : microphones_) {
    if (c.label == config_.preferred_microphone_name ||
        (config_.preferred_microphone_name.empty() && c.value == "default")) selected = static_cast<int>(microphones.size());
    microphones.push_back(c.label);
  }
  state_.SetMicrophones(std::move(microphones), selected);
}

bool ObsEngine::ConfigureCapture(CaptureTargetMode mode, const std::string& label, Error& error) {
  if (!initialized_) return true;
  {
    std::scoped_lock lock(save_mutex_);
    if (saving_ || queued_saves_) {
      error = Error{ErrorComponent::kCapture, "change OBS source",
          "Finish queued clip saves before changing capture source"};
      return false;
    }
  }
  const auto& choices = mode == CaptureTargetMode::kDisplay ? displays_ : games_;
  const SourceChoice* selected = nullptr;
  for (const auto& choice : choices) if (choice.label == label || choice.value == label) selected = &choice;
  if (!selected && !label.empty()) {
    // Migrate the previous "title [exe]" label only when the title identifies one OBS choice.
    const auto old_title = label.substr(0, label.find("  ["));
    const SourceChoice* match = nullptr;
    for (const auto& choice : choices) {
      if (choice.label.find(old_title) != std::string::npos) {
        if (match) { match = nullptr; break; }
        match = &choice;
      }
    }
    selected = match;
  }
  if (!selected && mode == CaptureTargetMode::kDisplay && label.starts_with("\\\\.\\DISPLAY")) {
    for (DWORD adapter_index = 0; ; ++adapter_index) {
      DISPLAY_DEVICEA adapter{}; adapter.cb = sizeof(adapter);
      if (!EnumDisplayDevicesA(nullptr, adapter_index, &adapter, 0)) break;
      if (!label.starts_with(adapter.DeviceName)) continue;
      for (DWORD monitor_index = 0; ; ++monitor_index) {
        DISPLAY_DEVICEA monitor{}; monitor.cb = sizeof(monitor);
        if (!EnumDisplayDevicesA(adapter.DeviceName, monitor_index, &monitor, EDD_GET_DEVICE_INTERFACE_NAME)) break;
        for (const auto& choice : choices)
          if (_stricmp(choice.value.c_str(), monitor.DeviceID) == 0) selected = &choice;
      }
    }
  }
  if (!selected && mode == CaptureTargetMode::kDisplay && label.empty() && !choices.empty()) selected = &choices.front();
  if (!selected && !label.empty() && mode == CaptureTargetMode::kDisplay) {
    error = Error{ErrorComponent::kCapture, "select OBS capture source", "The selected source is unavailable. Choose a currently running game or display.", {}, {}, label};
    return false;
  }
  auto settings = MakeData();
  const char* type = mode == CaptureTargetMode::kDisplay ? "monitor_capture" : "game_capture";
  if (mode == CaptureTargetMode::kDisplay) {
    if (!selected) { error = Error{ErrorComponent::kCapture, "select display", "OBS found no displays"}; return false; }
    obs_data_set_string(settings.get(), "monitor_id", selected->value.c_str());
    Properties properties(obs_get_source_properties(type), obs_properties_destroy);
    auto* method = properties ? obs_properties_get(properties.get(), "method") : nullptr;
    bool supported = false;
    if (method) for (std::size_t i = 0; i < obs_property_list_item_count(method); ++i)
      if (!obs_property_list_item_disabled(method, i) && obs_property_list_item_int(method, i) == config_.obs_display_method) supported = true;
    if (!supported) { error = Error{ErrorComponent::kCapture, "select display method", "The selected OBS display method is unsupported"}; return false; }
    obs_data_set_int(settings.get(), "method", config_.obs_display_method);
    obs_data_set_bool(settings.get(), "capture_cursor", config_.capture_cursor);
  } else {
    pending_game_label_ = !selected && !label.empty() ? label : "";
    obs_data_set_string(settings.get(), "capture_mode", selected || !label.empty() ? "window" : "any_fullscreen");
    if (selected) obs_data_set_string(settings.get(), "window", selected->value.c_str());
    obs_data_set_bool(settings.get(), "capture_cursor", config_.capture_cursor);
    obs_data_set_bool(settings.get(), "limit_framerate", config_.obs_limit_game_capture_fps);
    logger_.Info(std::string("OBS game capture copy limiter=") +
                 (config_.obs_limit_game_capture_fps ? "clip FPS" : "twice clip FPS (OBS default)"));
    obs_data_set_bool(settings.get(), "capture_audio", false); // Desktop audio is captured once.
    obs_data_set_bool(settings.get(), "anti_cheat_hook", true); // Official OBS compatibility mode only.
  }
  auto* next = obs_source_create(type, "Klip gameplay", settings.get(), nullptr);
  if (!next) { error = Error{ErrorComponent::kCapture, "create OBS source", std::string(type) + " creation failed"}; return false; }
  if (capture_item_) obs_sceneitem_remove(capture_item_);
  if (capture_) obs_source_release(capture_);
  capture_ = next;
  capture_started_ = std::chrono::steady_clock::now();
  display_fallback_attempted_ = false;
  capture_was_ready_ = false;
  capture_item_ = obs_scene_add(scene_, capture_);
  if (!capture_item_) { error = Error{ErrorComponent::kCapture, "add OBS capture to scene", "OBS could not add the selected capture source"}; return false; }
  obs_sceneitem_set_order(capture_item_, OBS_ORDER_MOVE_BOTTOM);
  layout_width_ = layout_height_ = 0;
  logger_.Info("OBS source lifecycle scene_showing=" + std::to_string(obs_source_showing(obs_scene_get_source(scene_))) +
               " capture_showing=" + std::to_string(obs_source_showing(capture_)) +
               " capture_active=" + std::to_string(obs_source_active(capture_)));
  config_.target_mode = mode;
  selected_source_id_ = selected ? selected->id : 0;
  state_.SetTarget(mode, selected ? selected->label : !pending_game_label_.empty() ? "Waiting for " + pending_game_label_ : "Automatic fullscreen game (OBS Game Capture)", selected_source_id_);
  logger_.Info(std::string("OBS capture type=") + type + " source=" + (selected ? selected->label : "automatic fullscreen"));
  if (replay_ && obs_output_active(replay_)) {
    StopOutput(replay_);
    replay_waiting_for_source_ = true;
  }
  return true;
}

bool ObsEngine::ConfigureOverlay(Error& error) {
  if (!config_.static_overlay_enabled && !config_.live_overlay_enabled) return true;
  auto settings = MakeData();
  const char* type;
  if (config_.static_overlay_enabled) {
    if (!std::filesystem::is_regular_file(config_.static_overlay_path)) {
      error = Error{ErrorComponent::kCapture, "load static overlay", "The selected overlay image does not exist"}; return false;
    }
    type = "image_source";
    obs_data_set_string(settings.get(), "file", PathToUtf8(config_.static_overlay_path).c_str());
    obs_data_set_bool(settings.get(), "unload", false);
  } else {
    type = "window_capture";
    const SourceChoice* selected = nullptr;
    for (const auto& choice : overlay_windows_)
      if (choice.label == config_.live_overlay_window_title || choice.value == config_.live_overlay_window_title) selected = &choice;
    if (!selected) {
      const auto old_title = config_.live_overlay_window_title.substr(0, config_.live_overlay_window_title.find("  ["));
      for (const auto& choice : overlay_windows_) {
        if (choice.label.find(old_title) == std::string::npos) continue;
        if (selected) { selected = nullptr; break; }
        selected = &choice;
      }
    }
    pending_overlay_label_ = selected ? "" : config_.live_overlay_window_title;
    obs_data_set_string(settings.get(), "window", selected ? selected->value.c_str() : "");
    Properties properties(obs_get_source_properties(type), obs_properties_destroy);
    if (!properties || !SetListInt(properties.get(), settings.get(), "method", 2) ||
        !SetListInt(properties.get(), settings.get(), "priority", 0)) {
      error = Error{ErrorComponent::kCapture, "configure live overlay",
                    "OBS Windows Graphics Capture with exact-title matching is unavailable on this system"}; return false;
    }
    obs_data_set_bool(settings.get(), "cursor", false);
    obs_data_set_bool(settings.get(), "client_area", true);
    obs_data_set_bool(settings.get(), "capture_audio", false);
  }
  overlay_ = obs_source_create_private(type, "Klip overlay", settings.get());
  if (!overlay_) { error = Error{ErrorComponent::kCapture, "create OBS overlay", "OBS overlay source creation failed"}; return false; }
  if (config_.static_overlay_enabled && !obs_source_get_width(overlay_)) {
    error = Error{ErrorComponent::kCapture, "decode static overlay", "OBS could not decode the selected image"}; return false;
  }
  if (config_.static_overlay_opacity < 1.0) {
    // Pinned OBS registers its float-opacity implementation as color_filter_v2.
    Properties properties(obs_get_source_properties("color_filter_v2"), obs_properties_destroy);
    auto* opacity = properties ? obs_properties_get(properties.get(), "opacity") : nullptr;
    if (!opacity || obs_property_get_type(opacity) != OBS_PROPERTY_FLOAT ||
        config_.static_overlay_opacity < obs_property_float_min(opacity) ||
        config_.static_overlay_opacity > obs_property_float_max(opacity)) {
      error = Error{ErrorComponent::kCapture, "configure overlay opacity", "OBS opacity filter does not expose the expected float property"}; return false;
    }
    auto filter_settings = MakeData();
    obs_data_set_double(filter_settings.get(), "opacity", config_.static_overlay_opacity);
    auto* filter = obs_source_create_private("color_filter_v2", "Klip overlay opacity", filter_settings.get());
    if (!filter) { error = Error{ErrorComponent::kCapture, "create overlay opacity", "OBS opacity filter creation failed"}; return false; }
    obs_source_filter_add(overlay_, filter);
    obs_source_release(filter);  // The source now owns the filter reference.
  }
  overlay_item_ = obs_scene_add(scene_, overlay_);
  if (!overlay_item_) { error = Error{ErrorComponent::kCapture, "compose OBS overlay", "OBS could not add the overlay to its scene"}; return false; }
  layout_width_ = layout_height_ = 0;
  UpdateSceneLayout();
  logger_.Info(std::string("OBS OVERLAY READY type=") + type + " opacity=" + std::to_string(config_.static_overlay_opacity));
  if (!pending_overlay_label_.empty()) state_.SetSettingsStatus(false, "Overlay waiting for window: " + pending_overlay_label_);
  return true;
}

void ObsEngine::UpdateSceneLayout() {
  obs_video_info video{};
  if (!obs_get_video_info(&video) || (layout_width_ == video.base_width && layout_height_ == video.base_height)) return;
  layout_width_ = video.base_width; layout_height_ = video.base_height;
  vec2 bounds{}, position{};
  if (capture_item_) {
    vec2_set(&bounds, static_cast<float>(video.base_width), static_cast<float>(video.base_height));
    obs_sceneitem_set_bounds_type(capture_item_, config_.scaling_mode == VideoScalingMode::kFit ? OBS_BOUNDS_SCALE_INNER : OBS_BOUNDS_STRETCH);
    obs_sceneitem_set_bounds(capture_item_, &bounds);
  }
  if (overlay_item_) {
    vec2_set(&position, static_cast<float>(config_.static_overlay_x * video.base_width),
                       static_cast<float>(config_.static_overlay_y * video.base_height));
    vec2_set(&bounds, static_cast<float>(config_.static_overlay_width * video.base_width),
                     static_cast<float>(config_.static_overlay_height * video.base_height));
    obs_sceneitem_set_alignment(overlay_item_, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
    obs_sceneitem_set_bounds_alignment(overlay_item_, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
    obs_sceneitem_set_pos(overlay_item_, &position);
    obs_sceneitem_set_bounds_type(overlay_item_, OBS_BOUNDS_STRETCH);
    obs_sceneitem_set_bounds(overlay_item_, &bounds);
    obs_sceneitem_set_order(overlay_item_, OBS_ORDER_MOVE_TOP);
  }
}

void ObsEngine::ConfigureAudio(const AppConfig& config) {
  if (!initialized_) return;
  auto configure = [&](obs_source_t*& source, obs_volmeter_t*& meter, std::atomic<float>& level,
                       bool enabled, const char* type, const char* name,
                       std::uint32_t channel, const std::string& device, float gain, std::uint32_t mixers) {
    if (!enabled) {
      obs_set_output_source(channel, nullptr);
      if (meter) { obs_volmeter_destroy(meter); meter = nullptr; }
      if (source) obs_source_release(source);
      source = nullptr;
      level = 0.0F;
      return;
    }
    auto settings = MakeData();
    obs_data_set_string(settings.get(), "device_id", device.c_str());
    obs_data_set_bool(settings.get(), "use_device_timing", std::string_view(type) == "wasapi_output_capture");
    if (!source) source = obs_source_create(type, name, settings.get(), nullptr);
    else obs_source_update(source, settings.get());
    if (source) {
      obs_source_set_audio_mixers(source, mixers);
      obs_source_set_volume(source, gain);
      obs_set_output_source(channel, source);
      if (!meter && dashboard_active_) {
        meter = obs_volmeter_create(OBS_FADER_LOG);
        if (meter) { obs_volmeter_add_callback(meter, MeterUpdated, &level); obs_volmeter_attach_source(meter, source); }
      }
    }
  };
  std::string device = "default";
  bool microphone_available = config.preferred_microphone_name.empty();
  for (const auto& choice : microphones_) if (choice.label == config.preferred_microphone_name) {device = choice.value; microphone_available = true;}
  if (config.microphone_enabled && !microphone_available) {
    const Error error{ErrorComponent::kAudio, "select microphone", "The configured microphone is disconnected. Choose an available device.", {}, {}, config.preferred_microphone_name};
    state_.SetError(error); logger_.ErrorMessage(error);
  }
  configure(desktop_, desktop_meter_, desktop_level_, config.desktop_audio_enabled, "wasapi_output_capture", "Klip desktop audio", 1, "default", static_cast<float>(config.desktop_audio_gain), config.obs_separate_audio_tracks ? 3 : 1);
  configure(microphone_, microphone_meter_, microphone_level_, config.microphone_enabled && microphone_available, "wasapi_input_capture", "Klip microphone", 2, device, static_cast<float>(config.microphone_audio_gain), config.obs_separate_audio_tracks ? 5 : 1);
  config_.desktop_audio_enabled = config.desktop_audio_enabled;
  config_.microphone_enabled = config.microphone_enabled;
  config_.preferred_microphone_name = config.preferred_microphone_name;
  state_.SetMicrophoneEnabled(config.microphone_enabled);
  state_.SetAudio(desktop_ != nullptr, microphone_ != nullptr, 0.0F, 0.0F);
}

void ObsEngine::SetDashboardActive(bool active) {
  if (dashboard_active_ == active) return;
  dashboard_active_ = active;
  if (initialized_) {
    UpdateAudioMeters();
    logger_.Info(active ? "OBS dashboard active; audio metering resumed" :
                         "OBS dashboard inactive; audio metering paused (capture/audio continue)");
  }
}

void ObsEngine::UpdateAudioMeters() {
  auto update = [&](obs_source_t* source, obs_volmeter_t*& meter, std::atomic<float>& level) {
    if (!dashboard_active_) {
      // Destroy only the visualization object. WASAPI and encoder references
      // remain untouched; capture volumes and tracks continue unchanged.
      if (meter) { obs_volmeter_destroy(meter); meter = nullptr; }
      level.store(0.0F, std::memory_order_relaxed);
    } else if (source && !meter) {
      meter = obs_volmeter_create(OBS_FADER_LOG);
      if (meter) {
        obs_volmeter_add_callback(meter, MeterUpdated, &level);
        obs_volmeter_attach_source(meter, source);
      }
    }
  };
  update(desktop_, desktop_meter_, desktop_level_);
  update(microphone_, microphone_meter_, microphone_level_);
}

bool ObsEngine::ConfigureEncoder(Error& error) {
  try {
    std::vector<std::string> available;
    const char* id = nullptr;
    for (std::size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
      if (std::string_view(id) == "obs_nvenc_h264_tex" || std::string_view(id) == "h264_texture_amf" ||
          std::string_view(id) == "obs_qsv11_v2" || std::string_view(id) == "obs_x264")
        available.emplace_back(id);
    }
    state_.SetAvailableEncoders(available);
    std::string selected = config_.obs_encoder_id;
    if (selected == "auto") {
      for (const char* candidate : {"obs_nvenc_h264_tex", "h264_texture_amf", "obs_qsv11_v2", "obs_x264"}) {
        if (std::find(available.begin(), available.end(), candidate) != available.end()) { selected = candidate; break; }
      }
    }
    if (std::find(available.begin(), available.end(), selected) == available.end())
      throw std::runtime_error("The selected OBS encoder is unavailable: " + selected);
    Data settings(obs_encoder_defaults(selected.c_str()), obs_data_release);
    Properties props(obs_get_encoder_properties(selected.c_str()), obs_properties_destroy);
    if (!settings || !props) throw std::runtime_error("Could not inspect OBS encoder properties");
    const bool performance = config_.encoder_quality == EncoderQuality::kPerformance;
    const bool maximum = config_.encoder_quality == EncoderQuality::kQuality;
    if (selected == "obs_nvenc_h264_tex") {
      const auto policy = ObsNvencPolicyFor(config_.encoder_quality);
      SetList(props.get(), settings.get(), "rate_control", "CQP");
      SetInt(props.get(), settings.get(), "cqp", config_.obs_cq);
      SetList(props.get(), settings.get(), "preset", policy.preset);
      SetList(props.get(), settings.get(), "tune", "hq");
      SetList(props.get(), settings.get(), "multipass", policy.multipass);
      SetList(props.get(), settings.get(), "profile", "high");
      SetInt(props.get(), settings.get(), "bf", 2);
      if (obs_properties_get(props.get(), "adaptive_quantization"))
        obs_data_set_bool(settings.get(), "adaptive_quantization", policy.adaptive_quantization);
      if (obs_properties_get(props.get(), "lookahead")) obs_data_set_bool(settings.get(), "lookahead", false);
    } else if (selected == "obs_x264") {
      SetList(props.get(), settings.get(), "rate_control", "CRF");
      SetInt(props.get(), settings.get(), "crf", config_.obs_cq);
      SetList(props.get(), settings.get(), "preset", performance ? "veryfast" : maximum ? "medium" : "fast");
      SetList(props.get(), settings.get(), "profile", "high");
    } else {
      SetList(props.get(), settings.get(), "rate_control", "CQP");
      for (const char* key : {"cqp", "qpi", "qpp", "qpb"}) SetInt(props.get(), settings.get(), key, config_.obs_cq);
      SetList(props.get(), settings.get(), "profile", "high");
      if (selected == "h264_texture_amf") SetList(props.get(), settings.get(), "preset", performance ? "speed" : maximum ? "quality" : "balanced");
      else SetList(props.get(), settings.get(), "target_usage", performance ? "TU7" : maximum ? "TU1" : "TU4");
    }
    SetInt(props.get(), settings.get(), "keyint_sec", 2);
    logger_.Info("OBS encoder=" + selected + " settings=" + obs_data_get_json(settings.get()));
    std::ofstream(config_.log_path.parent_path() / "obs-encoder-settings.json") << obs_data_get_json(settings.get());
    video_encoder_ = obs_video_encoder_create(selected.c_str(), "Klip shared video encoder", settings.get(), nullptr);
    if (!video_encoder_) throw std::runtime_error("OBS encoder creation failed: " + selected);
    obs_encoder_set_video(video_encoder_, obs_get_video());
    auto audio = MakeData();
    obs_data_set_int(audio.get(), "bitrate", config_.audio_bitrate / 1000);
    audio_encoder_ = obs_audio_encoder_create("ffmpeg_aac", "Klip shared AAC encoder", audio.get(), 0, nullptr);
    if (!audio_encoder_) throw std::runtime_error("OBS AAC encoder creation failed");
    obs_encoder_set_audio(audio_encoder_, obs_get_audio());
    if (config_.obs_separate_audio_tracks) {
      desktop_encoder_ = obs_audio_encoder_create("ffmpeg_aac", "Klip desktop-only AAC", audio.get(), 1, nullptr);
      microphone_encoder_ = obs_audio_encoder_create("ffmpeg_aac", "Klip microphone-only AAC", audio.get(), 2, nullptr);
      if (!desktop_encoder_ || !microphone_encoder_) throw std::runtime_error("OBS separate-track AAC creation failed");
      obs_encoder_set_audio(desktop_encoder_, obs_get_audio());
      obs_encoder_set_audio(microphone_encoder_, obs_get_audio());
    }
    state_.SetEncoder(selected);
    state_.SetEncoderStatus(selected == "obs_x264" ? "Software x264 selected; CPU encoding is active" : "OBS hardware encoder selected: " + selected);
    return true;
  } catch (const std::exception& exception) {
    error = Error{ErrorComponent::kVideoEncoder, "configure OBS encoder", exception.what()};
    return false;
  }
}

bool ObsEngine::StartReplayBuffer(Error& error) {
  auto settings = MakeData();
  obs_data_set_string(settings.get(), "directory", PathToUtf8(config_.output_directory).c_str());
  obs_data_set_string(settings.get(), "format", config_.obs_filename_format.c_str());
  obs_data_set_string(settings.get(), "extension", "mkv");
  obs_data_set_bool(settings.get(), "allow_spaces", false);
  obs_data_set_int(settings.get(), "max_time_sec", static_cast<long long>(config_.clip_duration_seconds));
  obs_data_set_int(settings.get(), "max_size_mb", static_cast<long long>(config_.rolling_buffer_bytes / (1024 * 1024)));
  replay_ = obs_output_create("replay_buffer", "Klip replay buffer", settings.get(), nullptr);
  if (!replay_) { error = Error{ErrorComponent::kRollingBuffer, "create OBS replay", "replay_buffer output is unavailable"}; return false; }
  obs_output_set_video_encoder(replay_, video_encoder_);
  obs_output_set_audio_encoder(replay_, audio_encoder_, 0);
  if (desktop_encoder_) obs_output_set_audio_encoder(replay_, desktop_encoder_, 1);
  if (microphone_encoder_) obs_output_set_audio_encoder(replay_, microphone_encoder_, 2);
  signal_handler_connect(obs_output_get_signal_handler(replay_), "saved", ReplaySaved, this);
  signal_handler_connect(obs_output_get_signal_handler(replay_), "stop", ReplayStopped, this);
  replay_waiting_for_source_ = true;
  state_.SetStatus(CaptureStatus::kStarting, "Waiting for OBS capture");
  return true;
}

bool ObsEngine::SaveReplayClip() {
  if (shutting_down_ || save_failed_ || !replay_ || !obs_output_active(replay_) ||
      !capture_ || !obs_source_get_width(capture_)) return false;
  std::scoped_lock lock(save_mutex_);
  if (queued_saves_ + static_cast<std::size_t>(saving_) >= config_.clip_request_queue_capacity) return false;
  ++queued_saves_;
  return true;
}

void ObsEngine::ReplaySaved(void* parameter, calldata*) noexcept {
  auto& self = *static_cast<ObsEngine*>(parameter);
  self.replay_saved_.store(true, std::memory_order_release);
  if (self.window_) PostMessageW(self.window_, WM_APP + 55, 0, 0);
}

void ObsEngine::ReplayStopped(void* parameter, calldata* data) noexcept {
  auto& self = *static_cast<ObsEngine*>(parameter);
  self.replay_stop_code_.store(static_cast<int>(calldata_int(data, "code")));
  if (self.window_) PostMessageW(self.window_, WM_APP + 55, 0, 0);
}

bool ObsEngine::StartRecording(Error& error) {
  ProcessOutputSignals();
  if (shutting_down_) { error = Error{ErrorComponent::kRecordingWriter, "start OBS recording", "Klip is shutting down"}; return false; }
  if (!replay_ || !obs_output_active(replay_)) { error = Error{ErrorComponent::kRecordingWriter, "start OBS recording", "Replay encoder is not active"}; return false; }
  if (recording_ && obs_output_active(recording_)) return true;
  if (recording_) {
    obs_output_release(recording_); recording_ = nullptr;
    ProcessOutputSignals();
    recording_reservation_.RemoveEmpty(); recording_reservation_.Forget();
  }
  std::string filename_format;
  if (!ReserveOutputFilename(config_.recording_directory, "recording_%CCYY-%MM-%DD_%hh-%mm-%ss",
                             recording_reservation_, filename_format, error,
                             ErrorComponent::kRecordingWriter)) return false;
  recording_path_ = recording_reservation_.Path();
  auto settings = MakeData();
  obs_data_set_string(settings.get(), "path", PathToUtf8(recording_path_).c_str());
  recording_ = obs_output_create("ffmpeg_muxer", "Klip recording", settings.get(), nullptr);
  if (!recording_) {
    recording_reservation_.RemoveEmpty(); recording_reservation_.Forget();
    error = Error{ErrorComponent::kRecordingWriter, "create OBS recording", "ffmpeg_muxer output is unavailable"}; return false;
  }
  obs_output_set_video_encoder(recording_, video_encoder_);
  obs_output_set_audio_encoder(recording_, audio_encoder_, 0);
  if (desktop_encoder_) obs_output_set_audio_encoder(recording_, desktop_encoder_, 1);
  if (microphone_encoder_) obs_output_set_audio_encoder(recording_, microphone_encoder_, 2);
  signal_handler_connect(obs_output_get_signal_handler(recording_), "stop", RecordingStopped, this);
  if (!obs_output_start(recording_)) {
    error = Error{ErrorComponent::kRecordingWriter, "start OBS recording", OutputError(recording_)};
    obs_output_release(recording_); recording_ = nullptr;
    recording_reservation_.RemoveEmpty(); recording_reservation_.Forget();
    return false;
  }
  recording_started_ = std::chrono::steady_clock::now();
  state_.SetRecording(true, false, recording_path_);
  logger_.Info("OBS RECORDING STARTED file=" + PathToUtf8(recording_path_));
  return true;
}

void ObsEngine::RecordingStopped(void* parameter, calldata* data) noexcept {
  auto& self = *static_cast<ObsEngine*>(parameter);
  const auto code = calldata_int(data, "code");
  self.recording_stop_event_.store(code == 0 ? 1 : static_cast<int>(code), std::memory_order_release);
  if (self.window_) PostMessageW(self.window_, WM_APP + 55, 0, 0);
}

void ObsEngine::StopRecording() {
  if (IsRecording()) {
    state_.SetRecording(false, true, recording_path_);
    obs_output_stop(recording_);
  }
}
bool ObsEngine::IsRecording() const { return recording_ && obs_output_active(recording_); }

void ObsEngine::MeterUpdated(void* parameter, const float*, const float* peak, const float*) noexcept {
  const float value = obs_db_to_mul(std::max(peak[0], peak[1]));
  static_cast<std::atomic<float>*>(parameter)->store(std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F, std::memory_order_relaxed);
}

void ObsEngine::SetOutputError(obs_output* output, const char* operation, ErrorComponent component) {
  Error error{component, operation, OutputError(output)};
  if (component == ErrorComponent::kRollingBuffer) state_.SetStatus(CaptureStatus::kFailed, operation);
  state_.SetError(error);
  logger_.ErrorMessage(error);
}

void ObsEngine::ProcessOutputSignals() {
  if (replay_stop_code_.exchange(0) != 0) {
    save_failed_ = true;
    { std::scoped_lock lock(save_mutex_); queued_saves_ = 0; }
    SetOutputError(replay_, "OBS replay stopped unexpectedly; restart buffering in settings");
  }
  if (replay_saved_.exchange(false, std::memory_order_acquire)) {
    const auto path = replay_reservation_.Path();
    std::error_code ec;
    const auto bytes = path.empty() ? 0 : std::filesystem::file_size(path, ec);
    if (ec || !bytes) {
      save_failed_ = true;
      state_.SetStatus(CaptureStatus::kFailed, "OBS replay save failed");
      const Error error{ErrorComponent::kClipWriter, "finish OBS replay save",
                        "OBS reported a save, but its reserved file is missing or empty"};
      state_.SetError(error); logger_.ErrorMessage(error);
    } else {
      const auto elapsed = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - save_started_).count();
      state_.SetLastSavedClip(path, elapsed);
      logger_.Info("OBS REPLAY SAVED file=" + PathToUtf8(path));
      // A completed file is no longer an empty-file cleanup candidate.
      replay_reservation_.Forget();
    }
    { std::scoped_lock lock(save_mutex_); saving_ = false; }
    if (!save_failed_) state_.SetStatus(CaptureStatus::kBuffering, "Replay buffer live");
  }
  const int recording_event = recording_stop_event_.exchange(0, std::memory_order_acquire);
  if (recording_event) {
    state_.SetRecording(false, false);
    std::error_code ec;
    const auto bytes = recording_path_.empty() ? 0 : std::filesystem::file_size(recording_path_, ec);
    if (recording_event == 1 && !ec && bytes) {
      state_.SetLastSavedRecording(recording_path_);
      logger_.Info("OBS RECORDING SAVED file=" + PathToUtf8(recording_path_));
      recording_reservation_.Forget();
    } else {
      SetOutputError(recording_, "recording stopped with an error or an empty output", ErrorComponent::kRecordingWriter);
    }
  }
}

void ObsEngine::ProcessSaveQueue() {
  if (!replay_) return;
  const bool ready = capture_ && obs_source_get_width(capture_) > 0 && obs_output_active(replay_);
  std::scoped_lock lock(save_mutex_);
  if (queued_saves_ && !saving_ && ready && !save_failed_) {
    --queued_saves_;
    std::string filename_format;
    Error filename_error;
    if (!ReserveOutputFilename(config_.output_directory, config_.obs_filename_format,
                               replay_reservation_, filename_format, filename_error,
                               ErrorComponent::kClipWriter)) {
      // Fail closed: never overwrite an existing file or repeatedly retry a bad path.
      save_failed_ = true; queued_saves_ = 0;
      state_.SetStatus(CaptureStatus::kFailed, "OBS output filename could not be reserved");
      state_.SetError(filename_error); logger_.ErrorMessage(filename_error); return;
    }
    auto settings = MakeData();
    obs_data_set_string(settings.get(), "format", filename_format.c_str());
    obs_output_update(replay_, settings.get());
    saving_ = true;
    save_started_ = std::chrono::steady_clock::now();
    state_.SetStatus(CaptureStatus::kSaving, "Saving OBS replay");
    calldata_t data{};
    const bool requested = proc_handler_call(obs_output_get_proc_handler(replay_), "save", &data);
    calldata_free(&data);
    if (!requested) {
      saving_ = false; save_failed_ = true; queued_saves_ = 0;
      SetOutputError(replay_, "request OBS replay save");
    }
  }
  if (saving_ && !save_failed_ &&
      std::chrono::steady_clock::now() - save_started_ > std::chrono::seconds(30)) {
    // Stock OBS has no replay-save error signal. Do not issue another request or
    // delete the reservation while its mux thread might still own the file.
    save_failed_ = true; queued_saves_ = 0;
    state_.SetStatus(CaptureStatus::kFailed, "OBS replay save timed out; restart buffering");
    const Error error{ErrorComponent::kClipWriter, "save OBS replay",
        "Save did not complete within 30 seconds. Check disk space, folder permissions and the OBS log. "
        "Restart buffering in settings before saving again; partial media is preserved."};
    state_.SetError(error); logger_.ErrorMessage(error);
  }
  if (!saving_ && ready && state_.Snapshot().status != CaptureStatus::kFailed)
    state_.SetStatus(CaptureStatus::kBuffering, "OBS replay buffer live");
  if (!ready && state_.Snapshot().status != CaptureStatus::kFailed)
    state_.SetStatus(CaptureStatus::kStarting, "Waiting for game capture; select Display for unsupported games");
}

void ObsEngine::Tick() {
  if (!initialized_ || !replay_) return;
  ProcessOutputSignals();
  const bool refresh_pending_sources = (!pending_game_label_.empty() || !pending_overlay_label_.empty()) &&
                                      std::chrono::steady_clock::now() >= next_source_refresh_;
  if (refresh_pending_sources) RefreshSources();
  if (!pending_game_label_.empty() && refresh_pending_sources) {
    const auto label = pending_game_label_;
    const auto old_title = label.substr(0, label.find("  ["));
    if (std::count_if(games_.begin(), games_.end(), [&](const auto& choice) { return choice.label == label || choice.label.find(old_title) != std::string::npos; }) == 1) {
      Error error;
      if (!ConfigureCapture(CaptureTargetMode::kGameWindow, label, error)) { state_.SetError(error); logger_.ErrorMessage(error); }
    }
  }
  if (!pending_overlay_label_.empty() && refresh_pending_sources) {
    const auto old_title = pending_overlay_label_.substr(0, pending_overlay_label_.find("  ["));
    const SourceChoice* match = nullptr;
    for (const auto& choice : overlay_windows_) {
      if (choice.label != pending_overlay_label_ && choice.label.find(old_title) == std::string::npos) continue;
      if (match) { match = nullptr; break; }
      match = &choice;
    }
    if (match && overlay_) {
      Data settings(obs_source_get_settings(overlay_), obs_data_release);
      obs_data_set_string(settings.get(), "window", match->value.c_str());
      obs_source_update(overlay_, settings.get());
      logger_.Info("OBS live overlay acquired: " + match->label);
      pending_overlay_label_.clear();
      state_.SetSettingsStatus(false, "Live overlay source acquired");
    }
  }
  if (refresh_pending_sources) next_source_refresh_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  if (capture_ && config_.target_mode == CaptureTargetMode::kDisplay &&
      config_.obs_display_method == 0 && !display_fallback_attempted_ &&
      obs_source_get_width(capture_) == 0 &&
      std::chrono::steady_clock::now() - capture_started_ > std::chrono::seconds(3)) {
    display_fallback_attempted_ = true;
    Properties properties(obs_source_properties(capture_), obs_properties_destroy);
    auto* method = properties ? obs_properties_get(properties.get(), "method") : nullptr;
    bool wgc_supported = false;
    if (method) for (std::size_t i = 0; i < obs_property_list_item_count(method); ++i)
      if (!obs_property_list_item_disabled(method, i) && obs_property_list_item_int(method, i) == 2) wgc_supported = true;
    if (wgc_supported) {
      Data settings(obs_source_get_settings(capture_), obs_data_release);
      obs_data_set_int(settings.get(), "method", 2);
      obs_source_update(capture_, settings.get());
      const std::string notice = "OBS display capture fallback: DXGI supplied no frames; Windows Graphics Capture is active";
      logger_.Warning(notice);
      state_.SetCaptureAdapter("OBS win-capture", notice);
      state_.SetEncoderStatus(notice + "; encoder quality unchanged");
    }
  }
  const bool source_ready = capture_ && obs_source_get_width(capture_) > 0;
  if (capture_was_ready_ && !source_ready && config_.target_mode == CaptureTargetMode::kGameWindow) {
    StopOutput(replay_);
    replay_waiting_for_source_ = true;
    logger_.Warning("OBS game capture closed or lost its target; replay history cleared while waiting for the game");
  }
  capture_was_ready_ = source_ready;
  if (native_resolution_pending_ && source_ready) {
    obs_video_info native{};
    obs_get_video_info(&native);
    const auto module = PathToUtf8(RuntimeRoot() / "bin/64bit/libobs-d3d11.dll");
    native.graphics_module = module.c_str();
    native.base_width = native.output_width = obs_source_get_width(capture_) & ~1U;
    native.base_height = native.output_height = obs_source_get_height(capture_) & ~1U;
    if (obs_reset_video(&native) != OBS_VIDEO_SUCCESS) {
      const Error error{ErrorComponent::kGraphics, "set native OBS resolution", "OBS could not configure the source resolution before encoding"};
      state_.SetError(error); logger_.ErrorMessage(error); replay_waiting_for_source_ = false; return;
    }
    obs_encoder_set_video(video_encoder_, obs_get_video());
    native_resolution_pending_ = false;
    logger_.Info("OBS native resolution locked: " + std::to_string(native.output_width) + "x" + std::to_string(native.output_height));
  }
  if (replay_waiting_for_source_ && source_ready) {
    replay_waiting_for_source_ = false;
    if (!obs_output_start(replay_)) { SetOutputError(replay_, "start OBS replay"); return; }
    replay_started_ = std::chrono::steady_clock::now();
  }
  UpdateSceneLayout();
  ProcessSaveQueue();
  state_.SetCaptureMetrics(obs_get_active_fps(), 0.0, obs_get_total_frames(),
                           obs_get_lagged_frames(), video_output_get_skipped_frames(obs_get_video()), 0, 0, 0.0);
  state_.SetAudio(desktop_ && obs_source_audio_active(desktop_), microphone_ && obs_source_audio_active(microphone_),
                  desktop_level_.load(std::memory_order_relaxed), microphone_level_.load(std::memory_order_relaxed));
  const auto buffered = obs_output_active(replay_) ? std::chrono::duration<double>(std::chrono::steady_clock::now() - replay_started_).count() : 0.0;
  state_.SetRollingMetrics(std::min(buffered, config_.clip_duration_seconds), 0, 0);
  if (IsRecording()) state_.SetRecording(true, false, recording_path_,
      std::chrono::duration<double>(std::chrono::steady_clock::now() - recording_started_).count());
}

void ObsEngine::StopOutput(obs_output* output) noexcept {
  if (!output || !obs_output_active(output)) return;
  obs_output_stop(output);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (obs_output_active(output) && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  if (obs_output_active(output)) obs_output_force_stop(output);
}

void ObsEngine::DrainAcceptedSaves() noexcept {
  // The UI has already stopped accepting commands. Keep capture/encoding live
  // long enough to finish accepted requests before releasing the OBS outputs.
  try {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
      ProcessOutputSignals();
      bool pending;
      { std::scoped_lock lock(save_mutex_); pending = saving_ || queued_saves_; }
      if (!pending || save_failed_) break;
      if (!replay_ || !obs_output_active(replay_) || !capture_ || !obs_source_get_width(capture_) ||
          std::chrono::steady_clock::now() >= deadline) {
        const Error error{ErrorComponent::kClipWriter, "finish queued clips on shutdown",
            "Capture became unavailable or the shutdown save deadline expired; unfinished requests could not be saved"};
        state_.SetError(error); logger_.ErrorMessage(error); break;
      }
      ProcessSaveQueue();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  } catch (...) { OutputDebugStringA("Klip: exception while draining OBS replay saves\n"); }
}

void ObsEngine::Shutdown() noexcept {
  shutting_down_ = true;
  DrainAcceptedSaves();
  StopOutput(recording_);
  StopOutput(replay_);
  try { ProcessOutputSignals(); } catch (...) {}
  if (recording_) { obs_output_release(recording_); recording_ = nullptr; }
  if (replay_) { obs_output_release(replay_); replay_ = nullptr; }
  // Output release joins stock OBS's replay mux thread. Consume any final signal
  // only after joining, and never hold save_mutex_ while calling release.
  try { ProcessOutputSignals(); } catch (...) {}
  replay_reservation_.RemoveEmpty(); replay_reservation_.Forget();
  recording_reservation_.RemoveEmpty(); recording_reservation_.Forget();
  if (video_encoder_) { obs_encoder_release(video_encoder_); video_encoder_ = nullptr; }
  if (audio_encoder_) { obs_encoder_release(audio_encoder_); audio_encoder_ = nullptr; }
  if (desktop_encoder_) { obs_encoder_release(desktop_encoder_); desktop_encoder_ = nullptr; }
  if (microphone_encoder_) { obs_encoder_release(microphone_encoder_); microphone_encoder_ = nullptr; }
  if (desktop_meter_) { obs_volmeter_destroy(desktop_meter_); desktop_meter_ = nullptr; }
  if (microphone_meter_) { obs_volmeter_destroy(microphone_meter_); microphone_meter_ = nullptr; }
  if (initialized_) for (std::uint32_t i = 0; i < 3; ++i) obs_set_output_source(i, nullptr);
  if (scene_) { obs_scene_release(scene_); scene_ = nullptr; capture_item_ = nullptr; overlay_item_ = nullptr; }
  if (capture_) { obs_source_release(capture_); capture_ = nullptr; }
  if (overlay_) { obs_source_release(overlay_); overlay_ = nullptr; }
  if (desktop_) { obs_source_release(desktop_); desktop_ = nullptr; }
  if (microphone_) { obs_source_release(microphone_); microphone_ = nullptr; }
  if (initialized_) obs_shutdown();
  if (!core_data_path_.empty()) { obs_remove_data_path(core_data_path_.c_str()); core_data_path_.clear(); }
  base_set_log_handler(nullptr, nullptr);
  initialized_ = false;
  replay_waiting_for_source_ = false;
  native_resolution_pending_ = false;
  capture_was_ready_ = false;
  pending_game_label_.clear();
  pending_overlay_label_.clear();
  layout_width_ = layout_height_ = 0;
  { std::scoped_lock lock(save_mutex_); queued_saves_ = 0; saving_ = false; }
}
}  // namespace klip
