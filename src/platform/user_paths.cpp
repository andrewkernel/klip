#include "klip/platform/user_paths.h"

#include <Windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace klip {
namespace {

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id,
                                  const std::filesystem::path& fallback) {
  PWSTR value = nullptr;
  const auto result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value);
  if (FAILED(result) || value == nullptr) return fallback;
  std::filesystem::path path(value);
  CoTaskMemFree(value);
  return path;
}

}  // namespace

UserPaths ResolveUserPaths() {
  std::wstring override_root(32768, L'\0');
  const auto override_length = GetEnvironmentVariableW(
      L"KLIP_DATA_ROOT", override_root.data(), static_cast<DWORD>(override_root.size()));
  if (override_length > 0 && override_length < override_root.size()) {
    override_root.resize(override_length);
    const std::filesystem::path root(override_root);
    return UserPaths{root / "settings.ini", root / "Clips", root / "Recordings",
                     root / "klip.log"};
  }
  const auto local = KnownFolder(FOLDERID_LocalAppData, std::filesystem::current_path());
  const auto videos = KnownFolder(FOLDERID_Videos, local);
  const auto settings_root = local / "Klip";
  const auto media_root = videos / "Klip";
  return UserPaths{settings_root / "settings.ini", media_root / "Clips",
                   media_root / "Recordings", settings_root / "klip.log"};
}

void ApplyUserPathDefaults(AppConfig& config, const UserPaths& paths) {
  config.output_directory = paths.clips_directory;
  config.recording_directory = paths.recordings_directory;
  config.log_path = paths.log_file;
}

}  // namespace klip
