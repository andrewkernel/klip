#include "klip/audio/audio_pipeline.h"

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

#include "klip/core/audio_timeline.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
}

namespace klip {
namespace {

constexpr int kMaximumBufferedFrames = AudioEncoder::kSampleRate;

}  // namespace

AudioPipeline::AudioPipeline(AudioEncoder& encoder, ApplicationState& state, Logger& logger)
    : encoder_(encoder), state_(state), logger_(logger) {}

AudioPipeline::~AudioPipeline() noexcept { Shutdown(); }

bool AudioPipeline::Initialize(std::int64_t qpc_origin, std::int64_t qpc_frequency,
                               const AppConfig& config, Error& error) {
  Shutdown();
  config_ = config;
  microphone_running_.store(false, std::memory_order_release);
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
  if (!encoder_.Initialize(config.audio_bitrate, error)) return false;
  running_.store(true, std::memory_order_release);
  if (!StartDesktop(error)) {
    running_.store(false, std::memory_order_release);
    encoder_.Shutdown();
    return false;
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
  sample_generation_.fetch_add(1, std::memory_order_release);
  mixer_cv_.notify_all();
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

bool AudioPipeline::OpenContext(CaptureContext& context, const std::wstring& device_id, DWORD flags,
                                Error& error) {
  StopContext(context);
  auto result = device_id.empty()
                    ? enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, context.device.put())
                    : enumerator_->GetDevice(device_id.c_str(), context.device.put());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "open audio device", result);
    return false;
  }
  result = context.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    context.client.put_void());
  if (FAILED(result)) {
    error = MakeHresultError(ErrorComponent::kAudio, "activate IAudioClient", result);
    return false;
  }
  result = context.client->GetMixFormat(&context.format);
  if (FAILED(result) || SampleFormat(context.format) == AV_SAMPLE_FMT_NONE) {
    error = Error{ErrorComponent::kAudio, "inspect audio format",
                  "WASAPI mix format is unsupported", {}, {}, {}};
    StopContext(context);
    return false;
  }
  context.ready_event.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
  if (!context.ready_event) {
    error = MakeWin32Error(ErrorComponent::kAudio, "create audio event", GetLastError());
    StopContext(context);
    return false;
  }
  result = context.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                      flags | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0,
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
  const auto swr_result = swr_alloc_set_opts2(
      &context.resampler, &output_layout, AV_SAMPLE_FMT_FLT, AudioEncoder::kSampleRate,
      &input_layout, SampleFormat(context.format), context.format->nSamplesPerSec, 0, nullptr);
  av_channel_layout_uninit(&input_layout);
  av_channel_layout_uninit(&output_layout);
  if (swr_result < 0 || swr_init(context.resampler) < 0) {
    error = Error{ErrorComponent::kAudio, "initialize audio resampler",
                  "libswresample rejected the WASAPI format", {}, {}, {}};
    StopContext(context);
    return false;
  }
  const auto sample_format = SampleFormat(context.format);
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
  if (!OpenContext(desktop_, L"", AUDCLNT_STREAMFLAGS_LOOPBACK, error)) return false;
  desktop_.thread = std::jthread(
      [this](std::stop_token token) { CaptureLoop(desktop_, desktop_buffer_, token); });
  return true;
}

bool AudioPipeline::StartMicrophone(int index, Error& error) {
  if (index < 0 || index >= static_cast<int>(microphones_.size())) return false;
  microphone_.kind = SourceKind::kMicrophone;
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
  try {
    if (FAILED(context.client->Start())) throw std::runtime_error("IAudioClient::Start failed");
    while (!stop_token.stop_requested() && running_.load(std::memory_order_acquire)) {
      if (WaitForSingleObject(context.ready_event.Get(), 100) != WAIT_OBJECT_0) continue;
      UINT32 packet_frames = 0;
      if (FAILED(context.capture->GetNextPacketSize(&packet_frames))) break;
      while (packet_frames > 0 && !stop_token.stop_requested()) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 device_position = 0;
        UINT64 qpc_position_100ns = 0;
        if (FAILED(context.capture->GetBuffer(&data, &frames, &flags, &device_position,
                                              &qpc_position_100ns)))
          break;
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
          const std::uint8_t* input[] = {data};
          converted = swr_convert(context.resampler, output, output_capacity, input,
                                  static_cast<int>(frames));
        }
        if (converted > 0) {
          const auto pts = qpc_position_100ns >= static_cast<UINT64>(qpc_origin_)
                               ? static_cast<std::int64_t>(qpc_position_100ns) - qpc_origin_
                               : Now100ns();
          Append(buffer, pts, context.converted.data(), converted,
                 (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0);
        }
        context.capture->ReleaseBuffer(frames);
        if (FAILED(context.capture->GetNextPacketSize(&packet_frames))) packet_frames = 0;
      }
    }
  } catch (const std::exception& exception) {
    Error error{ErrorComponent::kAudio,
                "WASAPI capture worker",
                exception.what(),
                {},
                {},
                context.kind == SourceKind::kDesktop ? "desktop" : "microphone"};
    state_.SetError(error);
    logger_.ErrorMessage(error);
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
        state_.SetAudio(
            Active(desktop_buffer_), Active(microphone_buffer_),
            std::clamp(Level(desktop_buffer_) * desktop_gain_.load(std::memory_order_acquire),
                       0.0F, 1.0F),
            std::clamp(Level(microphone_buffer_) *
                           microphone_gain_.load(std::memory_order_acquire),
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
      state_.SetAudio(
          Active(desktop_buffer_), Active(microphone_buffer_),
          std::clamp(Level(desktop_buffer_) * desktop_gain_.load(std::memory_order_acquire), 0.0F,
                     1.0F),
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
  mix(desktop_buffer_, desktop_gain_.load(std::memory_order_acquire));
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

AVSampleFormat AudioPipeline::SampleFormat(const WAVEFORMATEX* format) {
  if (format == nullptr) return AV_SAMPLE_FMT_NONE;
  WORD tag = format->wFormatTag;
  WORD bits = format->wBitsPerSample;
  if (tag == WAVE_FORMAT_EXTENSIBLE) {
    const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    if (extended->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) tag = WAVE_FORMAT_IEEE_FLOAT;
    if (extended->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) tag = WAVE_FORMAT_PCM;
    if (extended->Samples.wValidBitsPerSample != 0) bits = extended->Samples.wValidBitsPerSample;
  }
  if (tag == WAVE_FORMAT_IEEE_FLOAT && bits == 32) return AV_SAMPLE_FMT_FLT;
  if (tag == WAVE_FORMAT_PCM && bits == 16) return AV_SAMPLE_FMT_S16;
  if (tag == WAVE_FORMAT_PCM && bits == 32) return AV_SAMPLE_FMT_S32;
  return AV_SAMPLE_FMT_NONE;
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
