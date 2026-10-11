#pragma once
#include <Windows.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "klip/clips/trim_range.h"

namespace klip {
struct TrimImage {
  int width = 0, height = 0;
  std::vector<unsigned char> rgba;
};
struct TrimView {
  bool busy = false, loaded = false;
  double duration = 0, actual_start = 0, actual_end = 0;
  std::filesystem::path source, output;
  std::string message;
  std::vector<double> keyframes;
  std::shared_ptr<TrimImage> image;
  void* texture = nullptr;
};
// One cancellable worker, packet streaming, no re-encoding or full-clip pixel
// storage.
class ClipTrimmer {
 public:
  ~ClipTrimmer() { Shutdown(); }
  void Initialize(HWND window) { window_ = window; }
  bool Inspect(const std::filesystem::path& source);
  bool Preview(double position);
  bool Export(double start, double end);
  void Cancel();
  void Shutdown();
  TrimView Snapshot() const;

 private:
  void Publish(TrimView view);
  HWND window_ = nullptr;
  mutable std::mutex mutex_;
  TrimView view_;
  std::jthread worker_;
};
}  // namespace klip
