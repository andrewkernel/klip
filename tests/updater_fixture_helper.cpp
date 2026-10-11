// Separate test binary only. No registry override is compiled into the shipping
// helper.
#include <Windows.h>
#include <shellapi.h>
#include "klip/update/helper.h"
#include "klip/update/updater.h"
#include <fstream>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  wchar_t name[512]{};
  if (!GetEnvironmentVariableW(L"KLIP_UPDATER_TEST_REGISTRY", name, 512))
    return 90;
  HKEY root = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, name, 0, KEY_ALL_ACCESS, &root) !=
      ERROR_SUCCESS)
    return 91;
  if (RegOverridePredefKey(HKEY_CURRENT_USER, root) != ERROR_SUCCESS) {
    RegCloseKey(root);
    return 92;
  }
  int count = 0;
  auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  const auto code = klip::RunUpdateHelper(count, args, [] {
    std::ofstream(klip::InstalledKlipRoot() / "failure-notice.txt")
        << "Installer failed; original reopened.";
  });
  if (args) LocalFree(args);
  RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
  RegCloseKey(root);
  return code;
}
