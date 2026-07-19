#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

#include "klip/core/error.h"

namespace klip {

class Logger {
 public:
  Logger() = default;
  ~Logger() noexcept;

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  bool Open(const std::filesystem::path& path);
  void Close() noexcept;
  void Info(std::string_view message);
  void Warning(std::string_view message);
  void ErrorMessage(const Error& error);

 private:
  void Write(std::string_view level, std::string_view message);

  std::mutex mutex_;
  std::ofstream stream_;
};

}  // namespace klip
