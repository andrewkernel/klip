#include <Windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <iostream>

using Microsoft::WRL::ComPtr;

int wmain() {
  ComPtr<IDXGIFactory1> factory;
  auto result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(result)) {
    std::wcerr << L"CreateDXGIFactory1 failed: 0x" << std::hex << result << L'\n';
    return 1;
  }

  for (UINT adapter_index = 0;; ++adapter_index) {
    ComPtr<IDXGIAdapter1> adapter;
    result = factory->EnumAdapters1(adapter_index, &adapter);
    if (result == DXGI_ERROR_NOT_FOUND) break;
    if (FAILED(result)) {
      std::wcerr << L"EnumAdapters1 failed: 0x" << std::hex << result << L'\n';
      return 2;
    }

    DXGI_ADAPTER_DESC1 adapter_description{};
    if (FAILED(adapter->GetDesc1(&adapter_description))) continue;
    std::wcout << L"adapter=" << adapter_index << L" vendor=0x" << std::hex
               << adapter_description.VendorId << L" device=0x" << adapter_description.DeviceId
               << L" name=\"" << adapter_description.Description << L"\"\n";

    for (UINT output_index = 0;; ++output_index) {
      ComPtr<IDXGIOutput> output;
      result = adapter->EnumOutputs(output_index, &output);
      if (result == DXGI_ERROR_NOT_FOUND) break;
      if (FAILED(result)) {
        std::wcerr << L"EnumOutputs failed: 0x" << std::hex << result << L'\n';
        return 3;
      }

      DXGI_OUTPUT_DESC output_description{};
      if (FAILED(output->GetDesc(&output_description))) continue;
      MONITORINFOEXW monitor_info{};
      monitor_info.cbSize = sizeof(monitor_info);
      const bool monitor_info_available =
          GetMonitorInfoW(output_description.Monitor, &monitor_info) != FALSE;
      std::wcout << L"  output=" << output_index << L" display=" << output_description.DeviceName
                 << L" attached=" << (output_description.AttachedToDesktop ? L"yes" : L"no")
                 << L" primary="
                 << (monitor_info_available && (monitor_info.dwFlags & MONITORINFOF_PRIMARY) != 0
                         ? L"yes"
                         : L"no")
                 << L" rect=" << output_description.DesktopCoordinates.left << L','
                 << output_description.DesktopCoordinates.top << L' '
                 << output_description.DesktopCoordinates.right << L','
                 << output_description.DesktopCoordinates.bottom << L'\n';
    }
  }
  return 0;
}
