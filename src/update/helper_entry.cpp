#include <Windows.h>
#include <shellapi.h>
#include "klip/update/helper.h"
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int count = 0;
  auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!args) return 2;
  const auto result = klip::RunUpdateHelper(count, args);
  LocalFree(args);
  return result;
}
