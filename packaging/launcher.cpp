// GPL-3.0-or-later. Compatibility entry point for pre-2.0 shortcuts.
#include <windows.h>
#include <wchar.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int show) {
  wchar_t path[32768];
  const DWORD size = GetModuleFileNameW(nullptr, path, 32768);
  if (!size || size >= 32740) return 2;
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) return 2;
  wcscpy(slash + 1, L"bin\\64bit\\Klip.exe");
  HMODULE runtime = LoadLibraryExW(L"vcruntime140.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  HMODULE cpp = LoadLibraryExW(L"msvcp140.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!runtime || !cpp) {
    MessageBoxW(nullptr, L"Klip requires the Microsoft Visual C++ 2015-2022 x64 runtime.\nInstall it from Microsoft's official site, then open Klip again:\nhttps://aka.ms/vs/17/release/vc_redist.x64.exe\n\nDo not disable Windows security.", L"Klip — runtime required", MB_OK | MB_ICONINFORMATION);
    if (runtime) FreeLibrary(runtime);
    if (cpp) FreeLibrary(cpp);
    return 2;
  }
  FreeLibrary(runtime); FreeLibrary(cpp);
  const size_t length = wcslen(path) + wcslen(arguments) + 5;
  if (length >= 32768) return 2;
  auto* command = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), 0, length * sizeof(wchar_t)));
  if (!command) return 2;
  wcscpy(command, L"\""); wcscat(command, path); wcscat(command, L"\" "); wcscat(command, arguments);
  STARTUPINFOW startup{}; startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = static_cast<WORD>(show);
  PROCESS_INFORMATION child{};
  const BOOL started = CreateProcessW(path, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &child);
  HeapFree(GetProcessHeap(), 0, command);
  if (!started) {
    MessageBoxW(nullptr, L"Klip could not start. Extract the complete portable ZIP or reinstall the complete package; do not move Klip.exe out of its folder.", L"Klip — package incomplete", MB_OK | MB_ICONERROR);
    return 2;
  }
  CloseHandle(child.hThread);
  DWORD result = 0;
  if (wcsstr(arguments, L"--package-smoke-test")) {
    WaitForSingleObject(child.hProcess, INFINITE); GetExitCodeProcess(child.hProcess, &result);
  }
  CloseHandle(child.hProcess);
  return static_cast<int>(result);
}
