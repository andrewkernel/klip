#include "klip/core/logger.h"

#include <chrono>
#include <format>

namespace klip {

Logger::~Logger() noexcept { Close(); }

bool Logger::Open(const std::filesystem::path& path) {
  std::scoped_lock lock(mutex_);
  stream_.close();
  stream_.open(path, std::ios::out | std::ios::trunc);
  return stream_.is_open();
}

void Logger::Close() noexcept {
  std::scoped_lock lock(mutex_);
  if (stream_.is_open()) {
    stream_.flush();
    stream_.close();
  }
}

void Logger::Info(std::string_view message) { Write("INFO", message); }
void Logger::Warning(std::string_view message) { Write("WARN", message); }
void Logger::ErrorMessage(const Error& error) { Write("ERROR", error.ToString()); }

void Logger::Write(std::string_view level, std::string_view message) {
  std::scoped_lock lock(mutex_);
  if (!stream_.is_open()) {
    return;
  }
  const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
  stream_ << std::format("{:%Y-%m-%d %H:%M:%S} [{}] {}\n", now, level, message);
  stream_.flush();
}

}  // namespace klip
