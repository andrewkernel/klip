#include "klip/audio/audio_pipeline.h"

#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <sstream>
#include <stdexcept>

#include "klip/audio/audio_recovery_policy.h"
#include "klip/core/audio_timeline.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
}

namespace klip {
namespace {

constexpr int kMaximumBufferedFrames = AudioEncoder::kSampleRate;
constexpr wchar_t kProcessLoopbackDevice[] = L"VAD\\Process_Loopback";

enum class ProcessLoopbackMode : int { kInclude = 0, kExclude = 1 };
struct ProcessLoopbackParameters {
  DWORD target_process_id = 0;
  ProcessLoopbackMode mode = ProcessLoopbackMode::kExclude;
};
struct AudioActivationParameters {
  int activation_type = 1;  // AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK
  ProcessLoopbackParameters process_loopback;
};

class AudioActivationHandler final : public IActivateAudioInterfaceCompletionHandler {
 public:
  AudioActivationHandler() : completed_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
  ~AudioActivationHandler() {
    if (client_ != nullptr) client_->Release();
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
    if (output == nullptr) return E_POINTER;
    *output = nullptr;
    if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, IID_IAgileObject) ||
        IsEqualIID(id, __uuidof(IActivateAudioInterfaceCompletionHandler))) {
      *output = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto remaining = --references_;
    if (remaining == 0) delete this;
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE
  ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
    IUnknown* activated = nullptr;
    result_ = operation->GetActivateResult(&activation_result_, &activated);
    if (SUCCEEDED(result_) && SUCCEEDED(activation_result_) && activated != nullptr) {
      result_ =
          activated->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client_));
    } else if (SUCCEEDED(result_)) {
      result_ = activation_result_;
    }
    if (activated != nullptr) activated->Release();
    SetEvent(completed_.Get());
    return S_OK;
  }

  HRESULT Wait(winrt::com_ptr<IAudioClient>& client) {
    if (!completed_ || WaitForSingleObject(completed_.Get(), 5000) != WAIT_OBJECT_0)
      return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    if (FAILED(result_)) return result_;
    client.attach(client_);
    client_ = nullptr;
    return S_OK;
  }

 private:
  std::atomic<ULONG> references_{1};
  ScopedHandle completed_;
  HRESULT result_ = E_PENDING;
  HRESULT activation_result_ = E_PENDING;
  IAudioClient* client_ = nullptr;
};

HRESULT ActivateProcessLoopback(DWORD process_id, winrt::com_ptr<IAudioClient>& client) {
  AudioActivationParameters parameters{};
  parameters.process_loopback.target_process_id = process_id;
  parameters.process_loopback.mode = ProcessLoopbackMode::kExclude;
  PROPVARIANT activation{};
  activation.vt = VT_BLOB;
  activation.blob.cbSize = sizeof(parameters);
  activation.blob.pBlobData = reinterpret_cast<BYTE*>(&parameters);

  auto* handler = new AudioActivationHandler();
  winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> operation;
  const auto started = ActivateAudioInterfaceAsync(kProcessLoopbackDevice, __uuidof(IAudioClient),
                                                   &activation, handler, operation.put());
  if (FAILED(started)) {
    handler->Release();
    return started;
  }
  const auto result = handler->Wait(client);
  handler->Release();
  return result;
}

std::string ProcessName(DWORD process_id) {
  ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
  if (!process) return {};
  std::wstring path(32768, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process.Get(), 0, path.data(), &size)) return {};
  path.resize(size);
  const auto separator = path.find_last_of(L"\\/");
  const auto filename = separator == std::wstring::npos ? path : path.substr(separator + 1);
  if (filename.empty()) return {};
  const auto required =
      WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (required <= 0) return {};
  std::string output(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, output.data(), required, nullptr, nullptr);
  output.pop_back();
  return output;
}

}  // namespace

AudioPipeline::AudioPipeline(AudioEncoder& encoder, ApplicationState& state, Logger& logger)
    : encoder_(encoder), state_(state), logger_(logger) {}

AudioPipeline::~AudioPipeline() noexcept { Shutdown(); }

bool AudioPipeline::Initialize(std::int64_t qpc_origin, std::int64_t qpc_frequency,
                               const AppConfig& config, Error& error) {
  Shutdown();
  config_ = config;
  microphone_running_.store(false, std::memory_order_release);
  desktop_retry_after_ = {};
  microphone_retry_after_ = {};
  desktop_enabled_.store(config.desktop_audio_enabled, std::memory_order_release);
  desktop_gain_.store(static_cast<float>(std::clamp(config.desktop_audio_gain, 0.0, 2.0)),
                      std::memory_order_release);
  microphone_gain_.store(static_cast<float>(std::clamp(config.microphone_audio_gain, 0.0, 2.0)),
                         std::memory_order_release);
  qpc_frequency_ = qpc_frequency;
  qpc_origin_ = av_rescale(qpc_origin, 10'000'000LL, qpc_frequency);
  const auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), enumerator_.put_void());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "create audio-device enumerator", result);
    return false;
  }
  EnumerateMicrophones(error);  // Desktop audio remains usable without a mic.
  Error applications_error;
  if (!EnumerateAudioApplications(applications_error))
    logger_.Warning(applications_error.ToString());
  if (!encoder_.Initialize(config.audio_bitrate, error)) return false;
  running_.store(true, std::memory_order_release);
  if (config.desktop_audio_enabled) {
    if (!StartDesktop(error)) {
      logger_.Warning(
          error.ToString() +
          "; video capture will continue without desktop audio until it is enabled again");
      state_.SetError(error);
      error = {};
    }
  } else {
    logger_.Info("Desktop audio is disabled; capture is starting without an audio endpoint");
  }
  if (config.microphone_enabled && selected_microphone_ >= 0) {
    Error microphone_error;
    if (!StartMicrophone(selected_microphone_, microphone_error)) {
      logger_.Warning(microphone_error.ToString());
    } else {
      microphone_running_.store(true, std::memory_order_release);
    }
  }
  state_.SetMicrophoneEnabled(microphone_running_.load(std::memory_order_acquire));
  mixer_thread_ = std::jthread([this](std::stop_token token) { MixerLoop(token); });
  application_audio_thread_ =
      std::jthread([this](std::stop_token token) { ApplicationAudioLoop(token); });
  logger_.Info("WASAPI audio pipeline started");
  return true;
}

void AudioPipeline::Shutdown() noexcept {
  StopCapture();
  encoder_.Shutdown();
  enumerator_ = nullptr;
  logger_.Info("WASAPI audio pipeline stopped");
}

void AudioPipeline::StopCapture() noexcept {
  running_.store(false, std::memory_order_release);
  microphone_running_.store(false, std::memory_order_release);
  mixer_thread_.request_stop();
  application_audio_thread_.request_stop();
  sample_generation_.fetch_add(1, std::memory_order_release);
  mixer_cv_.notify_all();
  application_audio_thread_ = {};
  StopContext(desktop_);
  StopContext(microphone_);
  mixer_thread_ = {};
  encoder_.Flush();
  {
    std::scoped_lock lock(desktop_buffer_.mutex);
    desktop_buffer_.samples.clear();
    desktop_buffer_.read_pts = AV_NOPTS_VALUE;
  }
  {
    std::scoped_lock lock(microphone_buffer_.mutex);
    microphone_buffer_.samples.clear();
    microphone_buffer_.read_pts = AV_NOPTS_VALUE;
  }
  next_pts_ = AV_NOPTS_VALUE;
}

void AudioPipeline::SetMicrophoneEnabled(bool enabled) {
  std::scoped_lock lock(control_mutex_);
  if (!running_.load(std::memory_order_acquire)) return;
  if (!enabled) {
    microphone_running_.store(false, std::memory_order_release);
    StopContext(microphone_);
    std::scoped_lock buffer_lock(microphone_buffer_.mutex);
    microphone_buffer_.samples.clear();
    microphone_buffer_.read_pts = AV_NOPTS_VALUE;
  } else if (selected_microphone_ >= 0 && !microphone_.thread.joinable()) {
    Error error;
    if (!StartMicrophone(selected_microphone_, error)) {
      state_.SetError(error);
      logger_.ErrorMessage(error);
      return;
    }
    microphone_running_.store(true, std::memory_order_release);
  }
  const bool active = enabled && microphone_running_.load(std::memory_order_acquire);
  config_.microphone_enabled = active;
  state_.SetMicrophoneEnabled(active);
  logger_.Info(active ? "Microphone enabled" : "Microphone disabled");
}

void AudioPipeline::SetDesktopGain(float gain) noexcept {
  desktop_gain_.store(std::clamp(gain, 0.0F, 2.0F), std::memory_order_release);
}

void AudioPipeline::SetDesktopEnabled(bool enabled) {
  std::scoped_lock lock(control_mutex_);
  if (enabled && running_.load(std::memory_order_acquire) && !desktop_.thread.joinable()) {
    Error error;
    if (!StartDesktop(error)) {
      state_.SetError(error);
      logger_.ErrorMessage(error);
    } else {
      state_.ClearError(ErrorComponent::kAudio);
    }
  }
  desktop_enabled_.store(enabled, std::memory_order_release);
  config_.desktop_audio_enabled = enabled;
  logger_.Info(enabled ? "Desktop audio enabled" : "Desktop audio muted");
}

void AudioPipeline::SetExcludedApplication(std::string executable_name) {
  std::scoped_lock lock(control_mutex_);
  if (config_.excluded_audio_process == executable_name) return;
  config_.excluded_audio_process = std::move(executable_name);
  if (!running_.load(std::memory_order_acquire)) return;
  if (!desktop_enabled_.load(std::memory_order_acquire)) return;
  StopContext(desktop_);
  {
    std::scoped_lock buffer_lock(desktop_buffer_.mutex);
    desktop_buffer_.samples.clear();
    desktop_buffer_.read_pts = AV_NOPTS_VALUE;
  }
  Error error;
  Error applications_error;
  if (!EnumerateAudioApplications(applications_error))
    logger_.Warning(applications_error.ToString());
  if (!StartDesktop(error)) {
    state_.SetError(error);
    logger_.ErrorMessage(error);
  } else {
    if (config_.excluded_audio_process.empty()) {
      logger_.Info("Desktop audio records every application");
    } else if (excluded_process_active_.load(std::memory_order_acquire)) {
      logger_.Info("Desktop audio exclusion active: " + config_.excluded_audio_process);
    } else {
      logger_.Warning("Desktop audio exclusion is unavailable; recording complete desktop audio");
    }
  }
}

void AudioPipeline::SetMicrophoneGain(float gain) noexcept {
  microphone_gain_.store(std::clamp(gain, 0.0F, 2.0F), std::memory_order_release);
}

void AudioPipeline::SelectMicrophone(int index) {
  std::scoped_lock lock(control_mutex_);
  if (index < 0 || index >= static_cast<int>(microphones_.size()) || index == selected_microphone_)
    return;
  microphone_running_.store(false, std::memory_order_release);
  StopContext(microphone_);
  selected_microphone_ = index;
  std::vector<std::string> names;
  for (const auto& microphone : microphones_) names.push_back(microphone.name);
  state_.SetMicrophones(std::move(names), selected_microphone_);
  if (running_.load(std::memory_order_acquire) && config_.microphone_enabled) {
    Error error;
    if (!StartMicrophone(index, error)) {
      state_.SetError(error);
      logger_.ErrorMessage(error);
    } else {
      microphone_running_.store(true, std::memory_order_release);
    }
  }
}

bool AudioPipeline::EnumerateMicrophones(Error& error) {
  microphones_.clear();
  winrt::com_ptr<IMMDeviceCollection> collection;
  auto result = enumerator_->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, collection.put());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "enumerate microphones", result);
    return false;
  }
  UINT count = 0;
  collection->GetCount(&count);
  for (UINT index = 0; index < count; ++index) {
    winrt::com_ptr<IMMDevice> device;
    LPWSTR id = nullptr;
    if (FAILED(collection->Item(index, device.put())) || FAILED(device->GetId(&id))) continue;
    winrt::com_ptr<IPropertyStore> properties;
    std::wstring name = L"Microphone";
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.put()))) {
      PROPVARIANT value{};
      PropVariantInit(&value);
      if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
          value.vt == VT_LPWSTR)
        name = value.pwszVal;
      PropVariantClear(&value);
    }
    microphones_.push_back({id, WideToUtf8(name)});
    CoTaskMemFree(id);
  }
  selected_microphone_ = microphones_.empty() ? -1 : 0;
  if (!config_.preferred_microphone_name.empty()) {
    const auto preferred =
        std::find_if(microphones_.begin(), microphones_.end(), [&](const auto& microphone) {
          return microphone.name == config_.preferred_microphone_name;
        });
    if (preferred != microphones_.end())
      selected_microphone_ = static_cast<int>(preferred - microphones_.begin());
  }
  std::vector<std::string> names;
  for (const auto& microphone : microphones_) names.push_back(microphone.name);
  state_.SetMicrophones(std::move(names), selected_microphone_);
  return true;
}

bool AudioPipeline::EnumerateAudioApplications(Error& error) {
  audio_applications_.clear();
  winrt::com_ptr<IMMDevice> device;
  auto result = enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put());
  if (FAILED(result))
    result = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, device.put());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "open render endpoint sessions", result);
    return false;
  }
  winrt::com_ptr<IAudioSessionManager2> manager;
  result =
      device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, manager.put_void());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "open audio session manager", result);
    return false;
  }
  winrt::com_ptr<IAudioSessionEnumerator> sessions;
  result = manager->GetSessionEnumerator(sessions.put());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "enumerate application audio", result);
    return false;
  }
  int count = 0;
  sessions->GetCount(&count);
  for (int index = 0; index < count; ++index) {
    winrt::com_ptr<IAudioSessionControl> control;
    if (FAILED(sessions->GetSession(index, control.put()))) continue;
    winrt::com_ptr<IAudioSessionControl2> control2;
    if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2), control2.put_void())))
      continue;
    DWORD process_id = 0;
    if (FAILED(control2->GetProcessId(&process_id)) || process_id == 0 ||
        process_id == GetCurrentProcessId())
      continue;
    auto name = ProcessName(process_id);
    if (name.empty()) continue;
    const auto duplicate = std::find_if(
        audio_applications_.begin(), audio_applications_.end(), [&](const auto& application) {
          return _stricmp(application.name.c_str(), name.c_str()) == 0;
        });
    if (duplicate == audio_applications_.end())
      audio_applications_.push_back({process_id, std::move(name)});
  }
  std::sort(audio_applications_.begin(), audio_applications_.end(),
            [](const auto& left, const auto& right) { return left.name < right.name; });
  state_.SetAudioApplications(audio_applications_);
  return true;
}

bool AudioPipeline::OpenContext(CaptureContext& context, const std::wstring& device_id, DWORD flags,
                                Error& error, DWORD excluded_process_id) {
  StopContext(context);
  HRESULT result = S_OK;
  if (excluded_process_id != 0) {
    result = ActivateProcessLoopback(excluded_process_id, context.client);
  } else {
    result = device_id.empty()
                 ? enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, context.device.put())
                 : enumerator_->GetDevice(device_id.c_str(), context.device.put());
  }
  // Some systems do not assign a separate multimedia endpoint. Fall back to the
  // console role so loopback capture remains available instead of failing startup.
  if (FAILED(result) && excluded_process_id == 0 && device_id.empty()) {
    context.device = nullptr;
    result = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, context.device.put());
  }
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "open audio device", result);
    return false;
  }
  if (excluded_process_id == 0)
    result = context.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      context.client.put_void());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "activate IAudioClient", result);
    return false;
  }
  if (excluded_process_id != 0) {
    // The process-loopback virtual device does not reliably expose GetMixFormat. Microsoft's
    // ApplicationLoopback sample supplies an explicit shared-mode format and asks WASAPI to
    // convert. Use Klip's native mixer format so this path avoids an extra libswresample pass.
    context.format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (context.format == nullptr) {
      error = Error{ErrorComponent::kAudio,
                    "allocate process-loopback format",
                    "CoTaskMemAlloc failed",
                    {},
                    {},
                    {}};
      StopContext(context);
      return false;
    }
    *context.format = {};
    context.format->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    context.format->nChannels = AudioEncoder::kChannels;
    context.format->nSamplesPerSec = AudioEncoder::kSampleRate;
    context.format->wBitsPerSample = 32;
    context.format->nBlockAlign =
        static_cast<WORD>(context.format->nChannels * context.format->wBitsPerSample / 8);
    context.format->nAvgBytesPerSec = context.format->nSamplesPerSec * context.format->nBlockAlign;
  } else {
    result = context.client->GetMixFormat(&context.format);
  }
  if (FAILED(result) || ResolveWaveSampleFormat(context.format) == AV_SAMPLE_FMT_NONE) {
    error = Error{ErrorComponent::kAudio,
                  "inspect audio format",
                  "WASAPI mix format is unsupported",
                  {},
                  {},
                  {}};
    StopContext(context);
    return false;
  }
  context.ready_event.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
  if (!context.ready_event) {
    error = MakeWin32Error(ErrorComponent::kAudio, "create audio event", GetLastError());
    StopContext(context);
    return false;
  }
  DWORD initialize_flags = flags | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
  if (excluded_process_id != 0)
    initialize_flags |=
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  result = context.client->Initialize(AUDCLNT_SHAREMODE_SHARED, initialize_flags, 0, 0,
                                      context.format, nullptr);
  if (SUCCEEDED(result)) {
    result = context.client->SetEventHandle(context.ready_event.Get());
  }
  if (SUCCEEDED(result)) {
    result = context.client->GetService(__uuidof(IAudioCaptureClient), context.capture.put_void());
  }
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "initialize WASAPI capture", result);
    StopContext(context);
    return false;
  }
  AVChannelLayout input_layout{};
  AVChannelLayout output_layout{};
  av_channel_layout_default(&input_layout, context.format->nChannels);
  av_channel_layout_default(&output_layout, AudioEncoder::kChannels);
  const auto swr_result = swr_alloc_set_opts2(&context.resampler, &output_layout, AV_SAMPLE_FMT_FLT,
                                              AudioEncoder::kSampleRate, &input_layout,
                                              ResolveWaveSampleFormat(context.format),
                                              context.format->nSamplesPerSec, 0, nullptr);
  av_channel_layout_uninit(&input_layout);
  av_channel_layout_uninit(&output_layout);
  if (swr_result < 0 || swr_init(context.resampler) < 0) {
    error = Error{ErrorComponent::kAudio,
                  "initialize audio resampler",
                  "libswresample rejected the WASAPI format",
                  {},
                  {},
                  {}};
    StopContext(context);
    return false;
  }
  const auto sample_format = ResolveWaveSampleFormat(context.format);
  std::ostringstream format_message;
  format_message << (context.kind == SourceKind::kDesktop ? "Desktop" : "Microphone")
                 << " audio format: " << context.format->nSamplesPerSec << " Hz, "
                 << context.format->nChannels << " channels, " << context.format->wBitsPerSample
                 << "-bit " << (sample_format == AV_SAMPLE_FMT_FLT ? "float" : "PCM");
  logger_.Info(format_message.str());
  return true;
}

bool AudioPipeline::StartDesktop(Error& error) {
  desktop_.kind = SourceKind::kDesktop;
  desktop_.failed.store(false, std::memory_order_release);
  excluded_process_active_.store(false, std::memory_order_release);
  DWORD excluded_process_id = 0;
  if (!config_.excluded_audio_process.empty()) {
    const auto match = std::find_if(
        audio_applications_.begin(), audio_applications_.end(), [&](const auto& application) {
          return _stricmp(application.name.c_str(), config_.excluded_audio_process.c_str()) == 0;
        });
    if (match != audio_applications_.end()) excluded_process_id = match->process_id;
  }
  excluded_process_candidate_id_.store(excluded_process_id, std::memory_order_release);
  if (!OpenContext(desktop_, L"", AUDCLNT_STREAMFLAGS_LOOPBACK, error, excluded_process_id)) {
    if (excluded_process_id == 0) return false;
    logger_.Warning(error.ToString() + "; falling back to complete desktop audio");
    if (!OpenContext(desktop_, L"", AUDCLNT_STREAMFLAGS_LOOPBACK, error)) return false;
  } else if (excluded_process_id != 0) {
    excluded_process_active_.store(true, std::memory_order_release);
    logger_.Info("Process-loopback exclusion initialized for " + config_.excluded_audio_process);
  }
  desktop_.thread = std::jthread(
      [this](std::stop_token token) { CaptureLoop(desktop_, desktop_buffer_, token); });
  return true;
}

void AudioPipeline::ApplicationAudioLoop(std::stop_token stop_token) noexcept {
  const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  try {
    while (!stop_token.stop_requested() && running_.load(std::memory_order_acquire)) {
      for (int tick = 0; tick < 20 && !stop_token.stop_requested(); ++tick)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (stop_token.stop_requested() || !running_.load(std::memory_order_acquire)) break;

      std::scoped_lock lock(control_mutex_);
      const auto recovery_now = std::chrono::steady_clock::now();
      const bool desktop_needs_recovery = audio_detail::ShouldRetryCapture(
          desktop_enabled_.load(std::memory_order_acquire),
          desktop_.failed.load(std::memory_order_acquire), desktop_.thread.joinable(), recovery_now,
          desktop_retry_after_);
      if (desktop_needs_recovery) {
        desktop_.failed.store(false, std::memory_order_release);
        StopContext(desktop_);
        {
          std::scoped_lock buffer_lock(desktop_buffer_.mutex);
          desktop_buffer_.samples.clear();
          desktop_buffer_.read_pts = AV_NOPTS_VALUE;
        }
        Error restart_error;
        if (StartDesktop(restart_error)) {
          desktop_retry_after_ = {};
          logger_.Info("Desktop audio endpoint recovered");
        } else {
          desktop_.failed.store(true, std::memory_order_release);
          desktop_retry_after_ = recovery_now + std::chrono::seconds(2);
          state_.SetError(restart_error);
          logger_.ErrorMessage(restart_error);
        }
      }

      const bool microphone_needs_recovery = audio_detail::ShouldRetryCapture(
          config_.microphone_enabled, microphone_.failed.load(std::memory_order_acquire),
          microphone_.thread.joinable(), recovery_now, microphone_retry_after_);
      if (microphone_needs_recovery) {
        microphone_.failed.store(false, std::memory_order_release);
        microphone_running_.store(false, std::memory_order_release);
        StopContext(microphone_);
        {
          std::scoped_lock buffer_lock(microphone_buffer_.mutex);
          microphone_buffer_.samples.clear();
          microphone_buffer_.read_pts = AV_NOPTS_VALUE;
        }
        Error enumeration_error;
        if (!EnumerateMicrophones(enumeration_error)) logger_.Warning(enumeration_error.ToString());
        Error restart_error;
        if (selected_microphone_ >= 0 && StartMicrophone(selected_microphone_, restart_error)) {
          microphone_running_.store(true, std::memory_order_release);
          microphone_retry_after_ = {};
          state_.SetMicrophoneEnabled(true);
          logger_.Info("Microphone endpoint recovered");
        } else {
          microphone_.failed.store(true, std::memory_order_release);
          microphone_retry_after_ = recovery_now + std::chrono::seconds(2);
          if (restart_error.operation.empty()) {
            restart_error = Error{ErrorComponent::kAudio, "recover microphone endpoint",
                                  "no active microphone endpoint is available"};
          }
          state_.SetMicrophoneEnabled(false);
          state_.SetError(restart_error);
          logger_.ErrorMessage(restart_error);
        }
      }
      const bool desktop_ready =
          !desktop_enabled_.load(std::memory_order_acquire) ||
          (desktop_.thread.joinable() && !desktop_.failed.load(std::memory_order_acquire));
      const bool microphone_ready =
          !config_.microphone_enabled || microphone_running_.load(std::memory_order_acquire);
      if (desktop_ready && microphone_ready) state_.ClearError(ErrorComponent::kAudio);

      Error applications_error;
      if (!EnumerateAudioApplications(applications_error)) {
        logger_.Warning(applications_error.ToString());
        continue;
      }
      DWORD candidate = 0;
      if (!config_.excluded_audio_process.empty()) {
        const auto match = std::find_if(
            audio_applications_.begin(), audio_applications_.end(), [&](const auto& application) {
              return _stricmp(application.name.c_str(), config_.excluded_audio_process.c_str()) ==
                     0;
            });
        if (match != audio_applications_.end()) candidate = match->process_id;
      }
      if (candidate == excluded_process_candidate_id_.load(std::memory_order_acquire)) continue;
      if (!desktop_enabled_.load(std::memory_order_acquire)) {
        excluded_process_candidate_id_.store(candidate, std::memory_order_release);
        continue;
      }

      StopContext(desktop_);
      {
        std::scoped_lock buffer_lock(desktop_buffer_.mutex);
        desktop_buffer_.samples.clear();
        desktop_buffer_.read_pts = AV_NOPTS_VALUE;
      }
      Error restart_error;
      if (!StartDesktop(restart_error)) {
        state_.SetError(restart_error);
        logger_.ErrorMessage(restart_error);
      } else if (candidate == 0) {
        logger_.Info("Muted application is not active; recording complete desktop audio");
      } else {
        logger_.Info("Muted application session refreshed: " + config_.excluded_audio_process);
      }
    }
  } catch (const std::exception& exception) {
    logger_.Warning(
        Error{ErrorComponent::kAudio, "monitor application audio", exception.what()}.ToString());
  }
  if (SUCCEEDED(com_result)) CoUninitialize();
}

bool AudioPipeline::StartMicrophone(int index, Error& error) {
  if (index < 0 || index >= static_cast<int>(microphones_.size())) return false;
  microphone_.kind = SourceKind::kMicrophone;
  microphone_.failed.store(false, std::memory_order_release);
  if (!OpenContext(microphone_, microphones_[index].id, 0, error)) return false;
  microphone_.thread = std::jthread(
      [this](std::stop_token token) { CaptureLoop(microphone_, microphone_buffer_, token); });
  return true;
}

void AudioPipeline::StopContext(CaptureContext& context) noexcept {
  context.thread.request_stop();
  if (context.ready_event) SetEvent(context.ready_event.Get());
  if (context.client != nullptr) context.client->Stop();
  context.thread = {};
  if (context.resampler != nullptr) swr_free(&context.resampler);
  if (context.format != nullptr) {
    CoTaskMemFree(context.format);
    context.format = nullptr;
  }
  context.ready_event.Reset();
  context.converted.clear();
  context.capture = nullptr;
  context.client = nullptr;
  context.device = nullptr;
}

void AudioPipeline::CaptureLoop(CaptureContext& context, SourceBuffer& buffer,
                                std::stop_token stop_token) noexcept {
  const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool worker_failed = false;
  const auto report_failure = [&](Error error) {
    if (stop_token.stop_requested()) return;
    worker_failed = true;
    context.failed.store(true, std::memory_order_release);
    state_.SetError(error);
    logger_.ErrorMessage(error);
  };
  try {
    std::vector<std::int32_t> normalized_pcm24;
    if (FAILED(com_result)) {
      report_failure(
          MakeHresultError(ErrorComponent::kAudio, "initialize audio capture thread", com_result));
      return;
    }
    const auto start_result = context.client->Start();
    if (FAILED(start_result)) {
      report_failure(
          MakeHresultError(ErrorComponent::kAudio, "start WASAPI capture", start_result,
                           context.kind == SourceKind::kDesktop ? "desktop" : "microphone"));
      if (SUCCEEDED(com_result)) CoUninitialize();
      return;
    }
    while (!stop_token.stop_requested() && running_.load(std::memory_order_acquire)) {
      const auto wait_result = WaitForSingleObject(context.ready_event.Get(), 100);
      if (wait_result == WAIT_TIMEOUT) continue;
      if (wait_result == WAIT_FAILED) {
        report_failure(
            MakeWin32Error(ErrorComponent::kAudio, "wait for WASAPI audio packet", GetLastError(),
                           context.kind == SourceKind::kDesktop ? "desktop" : "microphone"));
        break;
      }
      UINT32 packet_frames = 0;
      auto result = context.capture->GetNextPacketSize(&packet_frames);
      if (FAILED(result)) {
        report_failure(
            MakeHresultError(ErrorComponent::kAudio, "query WASAPI packet size", result,
                             context.kind == SourceKind::kDesktop ? "desktop" : "microphone"));
        break;
      }
      while (packet_frames > 0 && !stop_token.stop_requested()) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 device_position = 0;
        UINT64 qpc_position_100ns = 0;
        result = context.capture->GetBuffer(&data, &frames, &flags, &device_position,
                                            &qpc_position_100ns);
        if (FAILED(result)) {
          report_failure(
              MakeHresultError(ErrorComponent::kAudio, "read WASAPI audio packet", result,
                               context.kind == SourceKind::kDesktop ? "desktop" : "microphone"));
          break;
        }
        const auto output_capacity = static_cast<int>(av_rescale_rnd(
            swr_get_delay(context.resampler, context.format->nSamplesPerSec) + frames,
            AudioEncoder::kSampleRate, context.format->nSamplesPerSec, AV_ROUND_UP));
        context.converted.resize(static_cast<std::size_t>(output_capacity) *
                                 AudioEncoder::kChannels);
        int converted = 0;
        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
          converted =
              static_cast<int>(av_rescale_rnd(frames, AudioEncoder::kSampleRate,
                                              context.format->nSamplesPerSec, AV_ROUND_NEAR_INF));
          std::fill(context.converted.begin(), context.converted.end(), 0.0F);
        } else {
          std::uint8_t* output[] = {reinterpret_cast<std::uint8_t*>(context.converted.data())};
          const std::uint8_t* input_data = data;
          if (context.format->wBitsPerSample == 24) {
            if (!ConvertPackedPcm24ToS32(context.format, data, frames, normalized_pcm24)) {
              context.capture->ReleaseBuffer(frames);
              throw std::runtime_error("could not normalize packed 24-bit WASAPI samples");
            }
            input_data = reinterpret_cast<const std::uint8_t*>(normalized_pcm24.data());
          }
          const std::uint8_t* input[] = {input_data};
          converted = swr_convert(context.resampler, output, output_capacity, input,
                                  static_cast<int>(frames));
          if (converted < 0) {
            context.capture->ReleaseBuffer(frames);
            report_failure(
                MakeFfmpegError(ErrorComponent::kAudio, "convert WASAPI audio packet", converted,
                                context.kind == SourceKind::kDesktop ? "desktop" : "microphone"));
            break;
          }
        }
        if (converted > 0) {
          const auto pts = qpc_position_100ns >= static_cast<UINT64>(qpc_origin_)
                               ? static_cast<std::int64_t>(qpc_position_100ns) - qpc_origin_
                               : Now100ns();
          Append(buffer, pts, context.converted.data(), converted,
                 (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0);
        }
        result = context.capture->ReleaseBuffer(frames);
        if (FAILED(result)) {
          report_failure(
              MakeHresultError(ErrorComponent::kAudio, "release WASAPI audio packet", result,
                               context.kind == SourceKind::kDesktop ? "desktop" : "microphone"));
          break;
        }
        result = context.capture->GetNextPacketSize(&packet_frames);
        if (FAILED(result)) {
          report_failure(
              MakeHresultError(ErrorComponent::kAudio, "query WASAPI packet size", result,
                               context.kind == SourceKind::kDesktop ? "desktop" : "microphone"));
          break;
        }
      }
      if (worker_failed) break;
    }
  } catch (const std::exception& exception) {
    Error error{ErrorComponent::kAudio,
                "WASAPI capture worker",
                exception.what(),
                {},
                {},
                context.kind == SourceKind::kDesktop ? "desktop" : "microphone"};
    report_failure(error);
  } catch (...) {
    Error error{ErrorComponent::kAudio,
                "WASAPI capture worker",
                "an unknown error stopped audio capture",
                {},
                {},
                context.kind == SourceKind::kDesktop ? "desktop" : "microphone"};
    report_failure(error);
  }
  if (context.client != nullptr) context.client->Stop();
  if (SUCCEEDED(com_result)) CoUninitialize();
}

void AudioPipeline::MixerLoop(std::stop_token stop_token) noexcept {
  try {
    std::vector<float> mixed;
    while (!stop_token.stop_requested() && running_.load(std::memory_order_acquire)) {
      const auto frame_count = encoder_.FrameSize();
      const auto frame_duration = FramesTo100ns(frame_count);
      if (next_pts_ == AV_NOPTS_VALUE) next_pts_ = Now100ns();

      // Keep the encoder on a real-time clock even when an event-driven WASAPI source is idle.
      // A short look-behind gives active endpoints time to deliver the samples for this frame;
      // any source that still has no data is mixed as silence instead of starving every source.
      constexpr std::int64_t kMixLookBehind = 500'000;  // 50 ms in 100-ns units.
      auto now = Now100ns();
      if (now - next_pts_ > 5'000'000) next_pts_ = now - frame_duration;
      const auto due = next_pts_ + frame_duration + kMixLookBehind;
      if (now < due) {
        const auto wait = std::chrono::nanoseconds((due - now) * 100);
        std::unique_lock lock(mixer_mutex_);
        std::stop_callback wake(stop_token, [this] { mixer_cv_.notify_all(); });
        mixer_cv_.wait_for(lock, wait, [&stop_token] { return stop_token.stop_requested(); });
        const bool desktop_enabled = desktop_enabled_.load(std::memory_order_acquire);
        state_.SetAudio(
            desktop_enabled && Active(desktop_buffer_), Active(microphone_buffer_),
            desktop_enabled
                ? std::clamp(Level(desktop_buffer_) * desktop_gain_.load(std::memory_order_acquire),
                             0.0F, 1.0F)
                : 0.0F,
            std::clamp(Level(microphone_buffer_) * microphone_gain_.load(std::memory_order_acquire),
                       0.0F, 1.0F));
        continue;
      }

      std::int64_t pts = AV_NOPTS_VALUE;
      if (!MixFrame(mixed, frame_count, pts)) {
        next_pts_ += frame_duration;
        continue;
      }
      Error error;
      if (!encoder_.Encode(mixed, frame_count, pts, error)) {
        state_.SetError(error);
        logger_.ErrorMessage(error);
      }
      const bool desktop_enabled = desktop_enabled_.load(std::memory_order_acquire);
      state_.SetAudio(
          desktop_enabled && Active(desktop_buffer_), Active(microphone_buffer_),
          desktop_enabled
              ? std::clamp(Level(desktop_buffer_) * desktop_gain_.load(std::memory_order_acquire),
                           0.0F, 1.0F)
              : 0.0F,
          std::clamp(Level(microphone_buffer_) * microphone_gain_.load(std::memory_order_acquire),
                     0.0F, 1.0F));
    }
  } catch (const std::exception& exception) {
    Error error{ErrorComponent::kAudio, "audio mixer worker", exception.what(), {}, {}, {}};
    state_.SetError(error);
    logger_.ErrorMessage(error);
  }
}

bool AudioPipeline::MixFrame(std::vector<float>& output, int frame_count, std::int64_t& pts) {
  if (frame_count <= 0 || next_pts_ == AV_NOPTS_VALUE) return false;
  const bool desktop_required = desktop_enabled_.load(std::memory_order_acquire);
  const bool microphone_required = microphone_running_.load(std::memory_order_acquire);
  const auto frame_duration = FramesTo100ns(frame_count);
  std::scoped_lock lock(desktop_buffer_.mutex, microphone_buffer_.mutex);
  pts = next_pts_;

  const auto discard_before = [&](SourceBuffer& source) {
    if (source.read_pts >= pts || source.samples.empty()) return;
    const auto available = static_cast<int>(source.samples.size() / AudioEncoder::kChannels);
    const auto discard = std::min(
        available, AudioFramesFrom100nsCeil(pts - source.read_pts, AudioEncoder::kSampleRate));
    for (int frame = 0; frame < discard; ++frame) {
      source.samples.pop_front();
      source.samples.pop_front();
    }
    source.read_pts += FramesTo100ns(discard);
  };
  discard_before(desktop_buffer_);
  if (microphone_required) discard_before(microphone_buffer_);

  output.assign(static_cast<std::size_t>(frame_count) * AudioEncoder::kChannels, 0.0F);
  const auto mix = [&](SourceBuffer& source, float gain) {
    if (source.read_pts == AV_NOPTS_VALUE || source.samples.empty()) return;
    const int offset = std::max(0, PtsToFrames(source.read_pts - pts));
    if (offset >= frame_count) return;
    const int available = static_cast<int>(source.samples.size() / AudioEncoder::kChannels);
    const int take = std::min(frame_count - offset, available);
    for (int frame = 0; frame < take; ++frame) {
      for (int channel = 0; channel < AudioEncoder::kChannels; ++channel) {
        output[static_cast<std::size_t>(offset + frame) * AudioEncoder::kChannels + channel] +=
            source.samples.front() * gain;
        source.samples.pop_front();
      }
    }
    source.read_pts += FramesTo100ns(take);
  };
  if (desktop_required) mix(desktop_buffer_, desktop_gain_.load(std::memory_order_acquire));
  if (microphone_required)
    mix(microphone_buffer_, microphone_gain_.load(std::memory_order_acquire));
  for (auto& sample : output) sample = std::clamp(sample, -1.0F, 1.0F);
  next_pts_ += frame_duration;
  return true;
}

void AudioPipeline::Append(SourceBuffer& buffer, std::int64_t pts, const float* samples, int frames,
                           bool discontinuity) {
  if (samples == nullptr || frames <= 0) return;
  std::scoped_lock lock(buffer.mutex);
  if (discontinuity) {
    buffer.samples.clear();
    buffer.read_pts = pts;
  }
  if (buffer.read_pts == AV_NOPTS_VALUE) buffer.read_pts = pts;
  const auto queued_frames = static_cast<int>(buffer.samples.size() / 2);
  const auto expected = buffer.read_pts + FramesTo100ns(queued_frames);
  const auto delta = PtsToFrames(pts - expected);
  int offset = 0;
  if (std::abs(delta) > kMaximumBufferedFrames) {
    buffer.samples.clear();
    buffer.read_pts = pts;
  } else if (delta > 0) {
    buffer.samples.insert(buffer.samples.end(), static_cast<std::size_t>(delta) * 2, 0.0F);
  } else if (delta < 0) {
    const auto overlap = std::min(-delta, frames);
    offset = overlap * 2;
    frames -= overlap;
  }
  float peak = 0.0F;
  for (int index = offset; index < offset + frames * 2; ++index) {
    peak = std::max(peak, std::abs(samples[index]));
    buffer.samples.push_back(samples[index]);
  }
  while (buffer.samples.size() > static_cast<std::size_t>(kMaximumBufferedFrames * 2)) {
    buffer.samples.pop_front();
    buffer.samples.pop_front();
    buffer.read_pts += FramesTo100ns(1);
  }
  const auto activity = pts + FramesTo100ns(frames);
  buffer.last_activity.store(activity, std::memory_order_release);
  buffer.peak.store(peak, std::memory_order_release);
  buffer.peak_time.store(activity, std::memory_order_release);
  sample_generation_.fetch_add(1, std::memory_order_release);
  mixer_cv_.notify_one();
}

float AudioPipeline::Level(const SourceBuffer& buffer) const {
  const auto age = Now100ns() - buffer.peak_time.load(std::memory_order_acquire);
  if (age >= 3'000'000) return 0.0F;
  const auto decay = std::clamp(1.0F - static_cast<float>(age) / 3'000'000.0F, 0.0F, 1.0F);
  return buffer.peak.load(std::memory_order_acquire) * decay;
}

bool AudioPipeline::Active(const SourceBuffer& buffer) const {
  const auto last = buffer.last_activity.load(std::memory_order_acquire);
  return last > 0 && Now100ns() - last < 5'000'000;
}

std::int64_t AudioPipeline::Now100ns() const noexcept {
  LARGE_INTEGER current{};
  QueryPerformanceCounter(&current);
  return av_rescale(current.QuadPart, 10'000'000LL, qpc_frequency_) - qpc_origin_;
}

AVSampleFormat ResolveWaveSampleFormat(const WAVEFORMATEX* format) {
  if (format == nullptr) return AV_SAMPLE_FMT_NONE;
  WORD tag = format->wFormatTag;
  WORD bits = format->wBitsPerSample;
  if (tag == WAVE_FORMAT_EXTENSIBLE) {
    if (format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
      return AV_SAMPLE_FMT_NONE;
    const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    if (extended->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) tag = WAVE_FORMAT_IEEE_FLOAT;
    if (extended->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) tag = WAVE_FORMAT_PCM;
    if (extended->Samples.wValidBitsPerSample > bits) return AV_SAMPLE_FMT_NONE;
    // Extensible PCM is left-aligned in its container. 24 valid bits inside a
    // 32-bit container are S32, not packed 24-bit samples.
  }
  if (tag == WAVE_FORMAT_IEEE_FLOAT && bits == 32) return AV_SAMPLE_FMT_FLT;
  if (tag == WAVE_FORMAT_PCM && bits == 16) return AV_SAMPLE_FMT_S16;
  if (tag == WAVE_FORMAT_PCM && bits == 24) return AV_SAMPLE_FMT_S32;
  if (tag == WAVE_FORMAT_PCM && bits == 32) return AV_SAMPLE_FMT_S32;
  return AV_SAMPLE_FMT_NONE;
}

bool ConvertPackedPcm24ToS32(const WAVEFORMATEX* format, const std::uint8_t* input,
                             std::uint32_t frames, std::vector<std::int32_t>& output) {
  if (format == nullptr || input == nullptr || format->nChannels == 0 ||
      format->wBitsPerSample != 24 || format->nBlockAlign < format->nChannels * 3U ||
      ResolveWaveSampleFormat(format) != AV_SAMPLE_FMT_S32) {
    return false;
  }
  const auto channel_count = static_cast<std::size_t>(format->nChannels);
  if (frames > output.max_size() / channel_count) return false;
  output.resize(static_cast<std::size_t>(frames) * channel_count);
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    const auto* frame_data = input + static_cast<std::size_t>(frame) * format->nBlockAlign;
    for (std::size_t channel = 0; channel < channel_count; ++channel) {
      const auto* sample_data = frame_data + channel * 3;
      std::int32_t sample = static_cast<std::int32_t>(sample_data[0]) |
                            (static_cast<std::int32_t>(sample_data[1]) << 8) |
                            (static_cast<std::int32_t>(sample_data[2]) << 16);
      if (sample >= 0x00800000) sample -= 0x01000000;
      output[static_cast<std::size_t>(frame) * channel_count + channel] = sample * 256;
    }
  }
  return true;
}

std::string AudioPipeline::WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const auto required =
      WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string output(static_cast<std::size_t>(std::max(0, required)), '\0');
  if (required > 0) {
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), required, nullptr, nullptr);
    output.pop_back();
  }
  return output;
}

std::int64_t AudioPipeline::FramesTo100ns(int frames) {
  return static_cast<std::int64_t>(frames) * 10'000'000LL / AudioEncoder::kSampleRate;
}

int AudioPipeline::PtsToFrames(std::int64_t pts) {
  return static_cast<int>(pts * AudioEncoder::kSampleRate / 10'000'000LL);
}

}  // namespace klip
