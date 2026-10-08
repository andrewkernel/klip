#pragma once

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <winrt/base.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "klip/core/application_state.h"
#include "klip/core/config.h"
#include "klip/core/error.h"
#include "klip/core/logger.h"
#include "klip/encoding/audio_encoder.h"
#include "klip/platform/scoped_handle.h"

extern "C" {
#include <libswresample/swresample.h>
}

namespace klip {

// Resolve the storage container, not merely the device's valid precision bits.
AVSampleFormat ResolveWaveSampleFormat(const WAVEFORMATEX* format);
bool ConvertPackedPcm24ToS32(const WAVEFORMATEX* format, const std::uint8_t* input,
                             std::uint32_t frames, std::vector<std::int32_t>& output);

class AudioPipeline {
 public:
  AudioPipeline(AudioEncoder& encoder, ApplicationState& state, Logger& logger);
  ~AudioPipeline() noexcept;

  AudioPipeline(const AudioPipeline&) = delete;
  AudioPipeline& operator=(const AudioPipeline&) = delete;

  bool Initialize(std::int64_t qpc_origin, std::int64_t qpc_frequency, const AppConfig& config,
                  Error& error);
  void Shutdown() noexcept;
  void StopCapture() noexcept;
  void SetMicrophoneEnabled(bool enabled);
  void SetDesktopEnabled(bool enabled);
  void SetExcludedApplication(std::string executable_name);
  void SetDesktopGain(float gain) noexcept;
  void SetMicrophoneGain(float gain) noexcept;
  void SelectMicrophone(int index);

 private:
  enum class SourceKind { kDesktop, kMicrophone };
  struct Microphone {
    std::wstring id;
    std::string name;
  };
  struct SourceBuffer {
    mutable std::mutex mutex;
    std::deque<float> samples;
    std::int64_t read_pts = AV_NOPTS_VALUE;
    std::atomic<std::int64_t> last_activity{0};
    std::atomic<float> peak{0.0F};
    std::atomic<std::int64_t> peak_time{0};
  };
  struct CaptureContext {
    SourceKind kind = SourceKind::kDesktop;
    winrt::com_ptr<IMMDevice> device;
    winrt::com_ptr<IAudioClient> client;
    winrt::com_ptr<IAudioCaptureClient> capture;
    WAVEFORMATEX* format = nullptr;
    SwrContext* resampler = nullptr;
    ScopedHandle ready_event;
    std::atomic<bool> failed{false};
    std::vector<float> converted;
    std::jthread thread;
  };

  bool EnumerateMicrophones(Error& error);
  bool EnumerateAudioApplications(Error& error);
  bool OpenContext(CaptureContext& context, const std::wstring& device_id, DWORD flags,
                   Error& error, DWORD excluded_process_id = 0);
  bool StartDesktop(Error& error);
  bool StartMicrophone(int index, Error& error);
  void StopContext(CaptureContext& context) noexcept;
  void CaptureLoop(CaptureContext& context, SourceBuffer& buffer,
                   std::stop_token stop_token) noexcept;
  void ApplicationAudioLoop(std::stop_token stop_token) noexcept;
  void MixerLoop(std::stop_token stop_token) noexcept;
  bool MixFrame(std::vector<float>& output, int frame_count, std::int64_t& pts);
  void Append(SourceBuffer& buffer, std::int64_t pts, const float* samples, int frames,
              bool discontinuity);
  float Level(const SourceBuffer& buffer) const;
  bool Active(const SourceBuffer& buffer) const;
  std::int64_t Now100ns() const noexcept;
  static std::string WideToUtf8(const std::wstring& value);
  static std::int64_t FramesTo100ns(int frames);
  static int PtsToFrames(std::int64_t pts);

  AudioEncoder& encoder_;
  ApplicationState& state_;
  Logger& logger_;
  AppConfig config_;
  std::int64_t qpc_origin_ = 0;
  std::int64_t qpc_frequency_ = 0;
  std::atomic<bool> running_{false};
  std::atomic<bool> microphone_running_{false};
  std::atomic<bool> desktop_enabled_{true};
  std::chrono::steady_clock::time_point desktop_retry_after_{};
  std::chrono::steady_clock::time_point microphone_retry_after_{};
  std::atomic<float> desktop_gain_{1.0F};
  std::atomic<float> microphone_gain_{1.0F};
  std::atomic<std::uint64_t> sample_generation_{0};
  std::atomic<DWORD> excluded_process_candidate_id_{0};
  std::atomic<bool> excluded_process_active_{false};
  mutable std::mutex control_mutex_;
  std::vector<Microphone> microphones_;
  std::vector<AudioApplicationOption> audio_applications_;
  int selected_microphone_ = -1;
  winrt::com_ptr<IMMDeviceEnumerator> enumerator_;
  CaptureContext desktop_;
  CaptureContext microphone_;
  SourceBuffer desktop_buffer_;
  SourceBuffer microphone_buffer_;
  std::jthread mixer_thread_;
  std::jthread application_audio_thread_;
  mutable std::mutex mixer_mutex_;
  std::condition_variable mixer_cv_;
  std::int64_t next_pts_ = AV_NOPTS_VALUE;
};

}  // namespace klip
