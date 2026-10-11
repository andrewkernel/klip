#pragma once
#include <Windows.h>

#include <filesystem>
#include <mutex>
#include <thread>
#include <functional>

#include "klip/update/manifest.h"

namespace klip {
enum class UpdatePhase {
  kIdle,
  kChecking,
  kCurrent,
  kAvailable,
  kDownloading,
  kReady,
  kStarting,
  kError
};
struct UpdateView {
  UpdatePhase phase = UpdatePhase::kIdle;
  std::string version, message = "Check for updates when you are ready.";
  float progress = 0;
  bool installed = false;
};
bool VerifyUpdateSignature(const UpdateManifest& manifest);
bool VerifyUpdateFile(const std::filesystem::path& path,
                      const UpdateManifest& manifest);
std::filesystem::path InstalledKlipRoot();
std::filesystem::path CurrentExecutable();
class Updater {
 public:
  using Receive = std::function<void(const unsigned char*, DWORD)>;
  using Transport = std::function<void(const std::wstring&, std::uint64_t,
                                       std::stop_token, const Receive&)>;
  // Test harnesses may inject a transport in code. The app has no runtime URL
  // override.
  explicit Updater(Transport transport = {})
      : transport_(std::move(transport)) {}
  ~Updater() { Shutdown(); }
  void Initialize(HWND window, const std::filesystem::path& cache);
  void Check();
  void Download();
  void Cancel();
  bool BeginInstall();
  bool InstallationReady();
  void Shutdown();
  UpdateView Snapshot() const;

 private:
  void Publish(UpdateView view);
  void FetchUpdate(const std::wstring& path, std::uint64_t limit,
                   std::stop_token stop, const Receive& receive);
  Transport transport_;
  HWND window_ = nullptr;
  std::filesystem::path cache_, installer_;
  std::optional<UpdateManifest> manifest_;
  mutable std::mutex mutex_;
  UpdateView view_;
  std::jthread worker_;
  HANDLE ready_ = nullptr, helper_ = nullptr;
};
void FetchUpdateHttps(const std::wstring& path, std::uint64_t limit,
                      std::stop_token stop, const Updater::Receive& receive);
}  // namespace klip
