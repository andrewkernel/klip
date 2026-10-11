#include <Windows.h>
#include <shellapi.h>

#include <chrono>
#include <fstream>
#include <iterator>

#include "klip/update/updater.h"
#include "klip/update/helper.h"

int klip::RunUpdateHelper(int count, wchar_t** args,
                          std::function<void()> report_failure) {
  try {
    if (!args || count != 4) {
      return 2;
    }
    wchar_t* end = nullptr;
    const auto pid = wcstoul(args[1], &end, 10);
    if (!pid || !end || *end) {
      return 2;
    }
    const std::wstring event_name = args[2];
    const std::filesystem::path installer = args[3];
    HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                FALSE, pid);
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name.c_str());
    if (!parent || !ready) {
      if (parent) CloseHandle(parent);
      if (ready) CloseHandle(ready);
      return 3;
    }
    const auto root = klip::InstalledKlipRoot();
    const auto exe = root / L"bin/64bit/Klip.exe";
    std::wstring parent_image(32768, L'\0');
    DWORD image_length = static_cast<DWORD>(parent_image.size());
    std::error_code parent_error;
    if (!QueryFullProcessImageNameW(parent, 0, parent_image.data(),
                                    &image_length))
      return 4;
    parent_image.resize(image_length);
    if (root.empty() ||
        !std::filesystem::equivalent(std::filesystem::path(parent_image), exe,
                                     parent_error) ||
        parent_error)
      return 4;
    const auto manifest_path =
        std::filesystem::path(installer.wstring() + L".manifest");
    std::error_code file_error;
    if (std::filesystem::file_size(manifest_path, file_error) > 4096 ||
        file_error) {
      CloseHandle(parent);
      CloseHandle(ready);
      return 4;
    }
    std::ifstream input(manifest_path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    const auto manifest = klip::ParseUpdateManifest(text, now);
    HANDLE held =
        CreateFileW(installer.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (root.empty() || !std::filesystem::is_regular_file(exe) || !manifest ||
        !klip::NewerVersion(manifest->version, KLIP_APP_VERSION) ||
        !klip::VerifyUpdateSignature(*manifest) ||
        held == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(held, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !klip::VerifyUpdateFile(installer, *manifest)) {
      if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
      CloseHandle(ready);
      CloseHandle(parent);
      return 4;
    }
    // Opened before the parent exits: PID reuse cannot make us wait for another
    // process.
    SetEvent(ready);
    CloseHandle(ready);
    if (WaitForSingleObject(parent, 30000) != WAIT_OBJECT_0) {
      CloseHandle(parent);
      CloseHandle(held);
      return 5;
    }
    CloseHandle(parent);
    std::wstring command = L"\"" + installer.wstring() +
                           L"\" /SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART "
                           L"/CLOSEAPPLICATIONS /DIR=\"" +
                           root.wstring() + L"\" /LOG=\"" +
                           installer.wstring() + L".log\"";
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION process{};
    DWORD code = 6;
    if (CreateProcessW(installer.c_str(), command.data(), nullptr, nullptr,
                       FALSE, 0, nullptr, root.c_str(), &start, &process)) {
      CloseHandle(process.hThread);
      WaitForSingleObject(process.hProcess, INFINITE);
      GetExitCodeProcess(process.hProcess, &code);
      CloseHandle(process.hProcess);
    }
    CloseHandle(held);
    // On failure the existing installation is reopened when possible; the
    // installer log is retained.
    if (code != 0 && report_failure)
      report_failure();
    else if (code != 0)
      MessageBoxW(nullptr,
                  L"The update could not be completed. Your clips and settings "
                  L"were not removed. Klip will reopen; retry the update or "
                  L"use the website installer.",
                  L"Klip update", MB_OK | MB_ICONWARNING);
    std::wstring restart = L"\"" + exe.wstring() + L"\"";
    if (CreateProcessW(exe.c_str(), restart.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, exe.parent_path().c_str(), &start, &process)) {
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
    }
    return static_cast<int>(code);
  } catch (...) {
    return 8;
  }
}
