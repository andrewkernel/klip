// Interactive QA harness: real panel/trimmer/GPU preview, no capture engine or
// hotkeys.
#include <Windows.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <imgui.h>
#include "klip/platform/win32_window.h"
#include "klip/graphics/d3d_device.h"
#include "klip/ui/imgui_host.h"
#include "klip/ui/main_panel.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  winrt::init_apartment(winrt::apartment_type::single_threaded);
  int argc = 0;
  auto args = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!args || argc != 2) {
    if (args) LocalFree(args);
    return 2;
  }
  const std::filesystem::path source = args[1];
  LocalFree(args);
  klip::Win32Window window;
  klip::D3dDevice graphics;
  klip::ImGuiHost imgui;
  klip::MainPanel panel;
  klip::ClipTrimmer trimmer;
  klip::Updater updater;
  klip::Error error;
  if (!window.Create(instance, show, error) ||
      !graphics.Initialize(window.Handle(), error) ||
      !imgui.Initialize(window.Handle(), graphics.Device(), graphics.Context(),
                        error))
    return 3;
  trimmer.Initialize(window.Handle());
  updater.Initialize(window.Handle(),source.parent_path()/L"QAUpdates");
  klip::AppConfig config;
  config.obs_replay_enabled = false;
  config.trim_after_save = true;
  klip::ApplicationSnapshot snapshot;
  snapshot.last_saved_clip = source;
  winrt::com_ptr<ID3D11ShaderResourceView> texture;
  std::shared_ptr<klip::TrimImage> image;
  klip::UiCommands commands;
  commands.trim_view = [&] {
    auto view = trimmer.Snapshot();
    view.texture = texture.get();
    return view;
  };
  commands.inspect_clip = [&](const auto& path) {
    return trimmer.Inspect(path);
  };
  commands.preview_trim = [&](double position) { trimmer.Preview(position); };
  commands.export_trim = [&](double start, double end) {
    trimmer.Export(start, end);
  };
  commands.cancel_trim = [&] { trimmer.Cancel(); };
  commands.update_view = [&] { return updater.Snapshot(); };
  commands.check_updates = [&] { updater.Check(); };
  commands.cancel_update = [&] { updater.Cancel(); };
  commands.save_settings = [&](const klip::AppConfig& desired) {
    config = desired;
    return true;
  };
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::minutes(10);
  while (window.PumpMessages() && std::chrono::steady_clock::now() < deadline) {
    const auto view = trimmer.Snapshot();
    if (view.image != image) {
      image = view.image;
      texture = nullptr;
      if (image) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = image->width;
        desc.Height = image->height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{image->rgba.data(),
                                    static_cast<UINT>(image->width * 4), 0};
        winrt::com_ptr<ID3D11Texture2D> pixels;
        if (SUCCEEDED(
                graphics.Device()->CreateTexture2D(&desc, &data, pixels.put())))
          graphics.Device()->CreateShaderResourceView(pixels.get(), nullptr,
                                                      texture.put());
      }
    }
    UINT width = 0, height = 0;
    if (window.TakeResize(width, height)) graphics.Resize(width, height, error);
    imgui.BeginFrame();
    panel.Render(snapshot, config, commands, false, {});
    constexpr float color[4]{.06F, .07F, .09F, 1};
    if (graphics.BeginFrame(color)) {
      imgui.Render();
      graphics.Present();
    }
    MsgWaitForMultipleObjectsEx(0, nullptr, 33, QS_ALLINPUT,
                                MWMO_INPUTAVAILABLE);
  }
  trimmer.Shutdown();
  updater.Shutdown();
  texture = nullptr;
  imgui.Shutdown();
  graphics.Shutdown();
  window.Destroy();
  winrt::uninit_apartment();
  return 0;
}
