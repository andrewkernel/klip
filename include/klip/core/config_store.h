#pragma once

#include <filesystem>
#include <string>

#include "klip/core/config.h"

namespace klip {

bool LoadConfig(const std::filesystem::path& path, AppConfig& config, std::string& diagnostic);
bool SaveConfig(const std::filesystem::path& path, const AppConfig& config,
                std::string& diagnostic);

}  // namespace klip
