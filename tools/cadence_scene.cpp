// A visible, v-synced D3D11 source with a binary ID in every presented frame.
// Escape closes it. No game hooks, input injection, or external assets.
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <mmsystem.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <string>
#include <fstream>
#include <vector>
#include "klip/platform/frame_waiter.h"

using Microsoft::WRL::ComPtr;
LRESULT CALLBACK SceneProc(HWND window, UINT message, WPARAM key, LPARAM value) {
  if (message == WM_CLOSE || (message == WM_KEYDOWN && key == VK_ESCAPE)) {
    DestroyWindow(window);
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, key, value);
}
class AudioMarker {
 public:
  explicit AudioMarker(bool enabled) {
    if (!enabled) return;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2; format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16; format.nBlockAlign = 4; format.nAvgBytesPerSec = 192000;
    if (waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { device_ = nullptr; return; }
    for (int sample = 0; sample < 2400; ++sample) {
      const auto value = static_cast<short>(5000.0 * std::sin(6.283185307179586 * 1000.0 * sample / 48000.0));
      samples_[sample * 2] = samples_[sample * 2 + 1] = value;
    }
    header_.lpData = reinterpret_cast<char*>(samples_);
    header_.dwBufferLength = sizeof(samples_);
    waveOutPrepareHeader(device_, &header_, sizeof(header_));
  }
  ~AudioMarker() {
    if (device_) { waveOutReset(device_); waveOutUnprepareHeader(device_, &header_, sizeof(header_)); waveOutClose(device_); }
  }
  void Play() {
    if (device_ && !(header_.dwFlags & WHDR_INQUEUE)) {
      start_sample_ = Position();
      marker_active_ = waveOutWrite(device_, &header_, sizeof(header_)) == MMSYSERR_NOERROR;
    }
  }
  bool Flash() {
    if (!marker_active_) return false;
    const auto position = Position();
    if (position >= start_sample_ + 2400) marker_active_ = false;
    return marker_active_ && position > start_sample_;
  }
 private:
  unsigned long Position() {
    MMTIME time{}; time.wType = TIME_SAMPLES;
    if (waveOutGetPosition(device_, &time, sizeof(time)) != MMSYSERR_NOERROR) return 0;
    if (time.wType == TIME_SAMPLES) return time.u.sample;
    if (time.wType == TIME_BYTES) return time.u.cb / 4;
    if (time.wType == TIME_MS) return time.u.ms * 48;
    return 0;
  }
  HWAVEOUT device_ = nullptr;
  WAVEHDR header_{};
  short samples_[4800]{};
  unsigned long start_sample_ = 0;
  bool marker_active_ = false;
};

int main(int argc, char** argv) {
  const int seconds = argc > 1 ? std::atoi(argv[1]) : 30;
  const bool resize_test = argc > 2 && std::string_view(argv[2]) == "--resize";
  const bool odd_size_test = argc > 2 && std::string_view(argv[2]) == "--odd-size";
  int source_fps = 0;
  bool av_markers = false;
  bool overlay_window = false;
  int present_interval = 1;
  int gpu_work = 0;
  bool smooth_workload = false;
  std::string frame_log, reference_first;
  for (int arg = 2; arg < argc; ++arg)
    if (std::string_view(argv[arg]).starts_with("--fps=")) source_fps = std::atoi(argv[arg] + 6);
    else if (std::string_view(argv[arg]) == "--av") av_markers = true;
    else if (std::string_view(argv[arg]) == "--overlay-window") overlay_window = true;
    else if (std::string_view(argv[arg]).starts_with("--present-interval=")) present_interval = std::atoi(argv[arg] + 19);
    else if (std::string_view(argv[arg]).starts_with("--frame-log=")) frame_log = argv[arg] + 12;
    else if (std::string_view(argv[arg]).starts_with("--reference-first=")) reference_first = argv[arg] + 18;
    else if (std::string_view(argv[arg]).starts_with("--gpu-work=")) gpu_work = std::atoi(argv[arg] + 11);
    else if (std::string_view(argv[arg]) == "--smooth-workload") smooth_workload = true;
  AudioMarker audio_marker(av_markers);
  long long marker_second = -1;
  if (source_fps < 0 || source_fps > 240) return 9;
  if (present_interval < 0 || present_interval > 1) return 9;
  if (seconds < 1 || seconds > 1800 || gpu_work < 0 || gpu_work > 256 || (gpu_work && overlay_window)) return 9;
  klip::FrameWaiter source_waiter;
  SetProcessDPIAware();
  WNDCLASSW klass{};
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpfnWndProc = SceneProc;
  klass.lpszClassName = L"KlipCadenceScene";
  RegisterClassW(&klass);
  int width = overlay_window ? 320 : odd_size_test ? 1281 : GetSystemMetrics(SM_CXSCREEN);
  int height = overlay_window ? 90 : odd_size_test ? 721 : GetSystemMetrics(SM_CYSCREEN);
  const int initial_width = width, initial_height = height;
  HWND window = CreateWindowExW(overlay_window ? WS_EX_TOPMOST : 0, klass.lpszClassName,
                                overlay_window ? L"Klip overlay test scene" : L"Klip cadence test scene",
                                WS_POPUP, overlay_window ? GetSystemMetrics(SM_CXSCREEN) - width : 0, 0,
                                width, height, nullptr, nullptr, klass.hInstance, nullptr);
  if (!window) return 1;
  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferCount = 2;
  desc.BufferDesc.Width = width;
  desc.BufferDesc.Height = height;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.OutputWindow = window;
  desc.SampleDesc.Count = 1;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGISwapChain> swap;
  if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr,
                                           0, D3D11_SDK_VERSION, &desc, &swap, &device, nullptr,
                                           &context)))
    return 2;
  ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(context.As(&context1))) return 3;
  ComPtr<ID3D11Texture2D> buffer;
  if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return 4;
  ComPtr<ID3D11RenderTargetView> target;
  if (FAILED(device->CreateRenderTargetView(buffer.Get(), nullptr, &target))) return 5;
  ComPtr<ID3D11VertexShader> load_vertex;
  ComPtr<ID3D11PixelShader> load_pixel;
  ComPtr<ID3D11Buffer> load_constants;
  if (gpu_work) {
    // Bounded fixed-iteration diagnostic load; no branches depend on the picture.
    constexpr char shader[] = R"(
      cbuffer Params : register(b0) { float2 size; float phase; uint work; uint smooth; float3 padding; };
      float4 vs(uint id : SV_VertexID) : SV_Position {
        float2 uv = float2((id << 1) & 2, id & 2);
        return float4(uv * float2(2,-2) + float2(-1,1), 0, 1);
      }
      float4 ps(float4 p : SV_Position) : SV_Target {
        float2 uv = p.xy / size;
        float2 z = frac(uv * 13 + float2(phase * .025, phase * .017));
        float energy = 0;
        [loop] for(uint i=0; i<work; ++i) {
          z = frac(z.yx * float2(1.731,1.613) + sin(z * float2(4.312,3.991) + i*.11));
          energy += dot(z,z);
        }
        float v = energy / max(work,1);
        if (smooth) {
          // Keep the expensive shader dependency but avoid incompressible
          // full-range per-pixel noise dominating encoder and replay memory.
          float r = .24+.16*sin(uv.x*24+uv.y*11+phase*.9);
          float g = .25+.16*sin(uv.y*29-uv.x*9+phase*.6);
          float b = .25+.15*cos(uv.x*21+uv.y*17-phase*.7);
          return float4(r+v*.005,g+v*.005,b+v*.005,1);
        }
        return float4(.08+.35*v, .10+.32*z.x, .12+.30*z.y, 1);
      }
    )";
    ComPtr<ID3DBlob> vertex_code, pixel_code, errors;
    auto compile = [&](const char* entry, const char* model, ComPtr<ID3DBlob>& code) {
      const HRESULT hr = D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, entry, model,
                                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
      if (FAILED(hr) && errors) std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(), stderr);
      return SUCCEEDED(hr);
    };
    if (!compile("vs", "vs_5_0", vertex_code) || !compile("ps", "ps_5_0", pixel_code)) return 12;
    if (FAILED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &load_vertex)) ||
        FAILED(device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(), nullptr, &load_pixel))) return 12;
    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = 32; constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER; constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&constants, nullptr, &load_constants))) return 12;
  }
  auto resize_target = [&](int new_width, int new_height) {
    context->OMSetRenderTargets(0, nullptr, nullptr);
    target.Reset();
    buffer.Reset();
    if (FAILED(swap->ResizeBuffers(0, new_width, new_height, DXGI_FORMAT_UNKNOWN, 0))) return false;
    if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return false;
    if (FAILED(device->CreateRenderTargetView(buffer.Get(), nullptr, &target))) return false;
    width = new_width;
    height = new_height;
    if (!SetWindowPos(window, nullptr, 0, 0, width, height,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW))
      return false;
    std::printf("resize=%dx%d\n", width, height);
    std::fflush(stdout);
    return true;
  };
  ShowWindow(window, SW_SHOW);
  SetForegroundWindow(window);
  std::printf("initial=%dx%d\n", initial_width, initial_height);
  std::fflush(stdout);
  const float black[4] = {0, 0, 0, 1}, white[4] = {1, 1, 1, 1}, blue[4] = {.05f, .1f, .25f, 1};
  auto rectangle = [&](int x, int y, int w, int h, const float* color) {
    D3D11_RECT rect{x * width / 1280, y * height / 720, (x + w) * width / 1280,
                    (y + h) * height / 720};
    context1->ClearView(target.Get(), color, &rect, 1);
  };
  const auto start = std::chrono::steady_clock::now();
  LARGE_INTEGER anchor{}, frequency{};
  QueryPerformanceCounter(&anchor); QueryPerformanceFrequency(&frequency);
  std::printf("trace_start_qpc=%lld qpc_frequency=%lld gpu_work=%d present_interval=%d requested_fps=%d\n",
              anchor.QuadPart, frequency.QuadPart, gpu_work, present_interval, source_fps);
  std::fflush(stdout);
  unsigned long long frame = 0;
  struct FrameSample { unsigned long long index; long long begin_ns, present_end_ns; bool flash; };
  std::vector<FrameSample> frame_samples;
  if (!frame_log.empty()) frame_samples.reserve(static_cast<std::size_t>(seconds) * (source_fps ? source_fps : 2000));
  bool resized = false;
  bool restored = false;
  bool running = true;
  while (running && std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) running = false;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!running) break;
    if (source_fps > 0) {
      source_waiter.WaitUntil(start + std::chrono::nanoseconds(frame * 1'000'000'000ULL / source_fps));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto frame_begin_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    bool flash = false;
    if (resize_test && !resized && elapsed >= std::chrono::seconds(4)) {
      if (!resize_target(std::max(320, (width / 2) & ~1), std::max(240, (height / 2) & ~1)))
        return 7;
      resized = true;
    } else if (resize_test && resized && !restored && elapsed >= std::chrono::seconds(8)) {
      if (!resize_target(GetSystemMetrics(SM_CXSCREEN) & ~1, GetSystemMetrics(SM_CYSCREEN) & ~1))
        return 8;
      restored = true;
    }
    if (overlay_window) {
      const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
      context->ClearRenderTargetView(target.Get(), milliseconds % 1000 < 500 ? white : black);
    } else {
      context->ClearRenderTargetView(target.Get(), blue);
      if (gpu_work) {
        struct Params { float width, height, phase; unsigned work, smooth; float padding[3]; };
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(load_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return 12;
        const Params parameters{static_cast<float>(width), static_cast<float>(height),
                               std::chrono::duration<float>(elapsed).count(), static_cast<unsigned>(gpu_work),
                               smooth_workload ? 1U : 0U, {0,0,0}};
        std::memcpy(mapped.pData, &parameters, sizeof(parameters));
        context->Unmap(load_constants.Get(), 0);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        context->RSSetViewports(1, &viewport);
        auto* render_target = target.Get();
        context->OMSetRenderTargets(1, &render_target, nullptr);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(load_vertex.Get(), nullptr, 0);
        context->PSSetShader(load_pixel.Get(), nullptr, 0);
        auto* constants = load_constants.Get();
        context->PSSetConstantBuffers(0, 1, &constants);
        context->Draw(3,0);
      }
      rectangle(0, 0, 1280, 96, black);
      for (int bit = 0; bit < 12; ++bit)
        if ((frame >> bit) & 1) rectangle(bit * 80, 8, 64, 64, white);
      rectangle(static_cast<int>((frame * 7) % 1200), 150, 80, 450, white);
    }
    if (av_markers) {
      const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
      if (milliseconds / 1000 != marker_second) { marker_second = milliseconds / 1000; audio_marker.Play(); }
      // MME queues sound ahead of playback. Show the marker at device playback,
      // not at waveOutWrite, so the fixture itself does not invent an A/V offset.
      flash = audio_marker.Flash();
      if (flash) rectangle(0, 640, 1280, 80, white);
    }
    if (frame == 0 && !reference_first.empty()) {
      // One startup-only GPU readback proves the actual source RGB values.
      // It is never repeated during the steady-state resource sample.
      D3D11_TEXTURE2D_DESC staging_desc{};
      buffer->GetDesc(&staging_desc);
      staging_desc.Usage = D3D11_USAGE_STAGING; staging_desc.BindFlags = 0;
      staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; staging_desc.MiscFlags = 0;
      ComPtr<ID3D11Texture2D> staging;
      if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, &staging))) return 10;
      context->CopyResource(staging.Get(), buffer.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 10;
      std::ofstream reference(reference_first, std::ios::binary);
      reference << "P6\n" << width << ' ' << height << "\n255\n";
      for (int y = 0; y < height; ++y) {
        const auto* row = static_cast<const char*>(mapped.pData) + y * mapped.RowPitch;
        for (int x = 0; x < width; ++x) reference.write(row + x * 4, 3);
      }
      const bool written = static_cast<bool>(reference);
      context->Unmap(staging.Get(), 0);
      if (!written) return 10;
    }
    if (FAILED(swap->Present(static_cast<UINT>(present_interval), 0))) return 6;
    if (!frame_log.empty()) {
      const auto end_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
      frame_samples.push_back({frame, frame_begin_ns, end_ns, flash});
    }
    ++frame;
  }
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("presented=%llu elapsed=%.6f source_fps=%.3f\n", frame, elapsed, frame / elapsed);
  if (!frame_log.empty()) {
    std::ofstream output(frame_log);
    output << "source_index,begin_ns,present_end_ns,flash\n";
    for (const auto& sample : frame_samples)
      output << sample.index << ',' << sample.begin_ns << ',' << sample.present_end_ns << ',' << sample.flash << '\n';
    if (!output) return 11;
  }
  DestroyWindow(window);
  return 0;
}
