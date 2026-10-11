#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <thread>
#include <stdexcept>
#include "klip/update/updater.h"

int main(int argc, char** argv) {
  try {
    const auto executable = klip::CurrentExecutable();
    const auto root = executable.parent_path().parent_path().parent_path();
    if (std::filesystem::exists(root / "updated.marker") ||
        (argc == 1 &&
         std::filesystem::exists(root / "expected-failure.marker"))) {
      std::ifstream settings(root / "settings-sentinel.ini");
      std::string saved;
      std::getline(settings, saved);
      std::ifstream clip(root / "clip-sentinel.mkv");
      std::string media;
      std::getline(clip, media);
      if (saved != "keep settings" || media != "keep clip") return 89;
      std::ofstream(root / (std::filesystem::exists(root / "updated.marker")
                                ? "restarted.ok"
                                : "recovered.ok"))
          << "Settings and clip retained; executable restarted.";
      return 0;
    }
    if (argc != 3) return 2;
    const std::filesystem::path fixture = argv[1];
    const std::string mode = argv[2];
    wchar_t registry[512]{};
    if (!GetEnvironmentVariableW(L"KLIP_UPDATER_TEST_REGISTRY", registry, 512))
      return 3;
    HKEY virtual_root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, registry, 0, KEY_ALL_ACCESS,
                      &virtual_root) != ERROR_SUCCESS)
      return 4;
    if (RegOverridePredefKey(HKEY_CURRENT_USER, virtual_root) != ERROR_SUCCESS)
      return 5;
    klip::Updater updater([&](const std::wstring& path, std::uint64_t limit,
                              std::stop_token stop,
                              const klip::Updater::Receive& receive) {
      const bool manifest = path == L"/updates/windows.manifest";
      const auto file = manifest
                            ? (mode == "current" ? fixture / "current.manifest"
                                                 : fixture / "update.manifest")
                            : fixture / "Klip-9.0.0-win64-setup.exe";
      std::ifstream stream(file, std::ios::binary);
      if (!stream) throw std::runtime_error("Fixture missing");
      std::array<unsigned char, 1024> bytes{};
      std::uint64_t total = 0;
      while (stream && !stop.stop_requested()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        const auto n = static_cast<DWORD>(stream.gcount());
        if (!n) break;
        total += n;
        if (total > limit) throw std::runtime_error("Response exceeded limit");
        if (!manifest && mode == "truncated" && total > 4096) return;
        if (!manifest && mode == "offline")
          throw std::runtime_error("Simulated network interruption");
        if (!manifest && mode == "corrupt") bytes[0] ^= 1;
        if (manifest && mode == "bad-signature" && total == n) bytes[20] ^= 1;
        receive(bytes.data(), n);
        if (!manifest && mode == "cancel")
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      if (stop.stop_requested()) throw std::runtime_error("Update cancelled");
    });
    const auto cache = root / "Updates";
    if (mode == "unwritable") std::ofstream(cache) << "not a directory";
    updater.Initialize(nullptr, cache);
    if (!updater.Snapshot().installed) return 6;
    const auto wait = [&](klip::UpdatePhase phase) {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(20);
      while (updater.Snapshot().phase == phase &&
             std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      return updater.Snapshot().phase != phase;
    };
    updater.Check();
    if (!wait(klip::UpdatePhase::kChecking)) return 7;
    if (mode == "bad-signature") {
      if (updater.Snapshot().phase != klip::UpdatePhase::kError) return 8;
      std::cout << "Rejected signed-manifest tampering\n";
      return 0;
    }
    if (mode == "current") {
      if (updater.Snapshot().phase != klip::UpdatePhase::kCurrent ||
          updater.BeginInstall())
        return 18;
      updater.Download();
      if (updater.Snapshot().phase != klip::UpdatePhase::kCurrent) return 19;
      std::cout << "Current release cannot download or reinstall\n";
      return 0;
    }
    if (updater.Snapshot().phase != klip::UpdatePhase::kAvailable) return 9;
    updater.Download();
    if (mode == "cancel") {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      updater.Cancel();
    }
    if (!wait(klip::UpdatePhase::kDownloading)) return 10;
    if (mode != "success" && mode != "installer-failure" &&
        mode != "tamper-after-download") {
      if (updater.Snapshot().phase != klip::UpdatePhase::kError) return 11;
      if (std::filesystem::is_directory(cache))
        for (const auto& entry : std::filesystem::directory_iterator(cache))
          if (entry.path().extension() == ".partial") return 12;
      if (updater.BeginInstall()) return 13;
      std::cout << "Rejected " << mode
                << "; no installer started; partial removed\n";
      return 0;
    }
    if (updater.Snapshot().phase != klip::UpdatePhase::kReady) return 14;
    if (mode == "tamper-after-download") {
      for (const auto& file : std::filesystem::directory_iterator(cache))
        if (file.path().extension() == ".exe") {
          std::fstream change(file.path(),
                              std::ios::binary | std::ios::in | std::ios::out);
          change.put('x');
        }
    }
    if (!updater.BeginInstall()) return 15;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!updater.InstallationReady() &&
           std::chrono::steady_clock::now() < deadline) {
      if (updater.Snapshot().phase == klip::UpdatePhase::kError) {
        if (mode == "tamper-after-download") {
          std::cout << "Helper rejected cached-file tampering; parent remained "
                       "open\n";
          return 0;
        }
        return 16;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (mode == "tamper-after-download") return 20;
    if (!updater.InstallationReady()) return 17;
    updater.Shutdown();
    std::cout << "Verified download, helper ready, parent exiting for update\n";
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    RegCloseKey(virtual_root);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 99;
  }
}
