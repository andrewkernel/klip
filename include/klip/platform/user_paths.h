#pragma once

#include <filesystem>

#include "klip/core/config.h"

namespace klip {

struct UserPaths {
  std::filesystem::path settings_file;
  std::filesystem::path clips_directory;
  std::filesystem::path recordings_directory;
  std::filesystem::path log_file;
};

UserPaths ResolveUserPaths();
void ApplyUserPathDefaults(AppConfig& config, const UserPaths& paths);

}  // namespace klip
