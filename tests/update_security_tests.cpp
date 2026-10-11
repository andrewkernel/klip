#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "klip/update/updater.h"

int main() {
  const std::string fixture =
      "klip-update-v1\n2.0.2\nhttps://klip-ten-zeta.vercel.app/"
      "Klip-2.0.2-win64-setup.exe\n"
      "de2f256064a0af797747c2b97505dc0b9f3df0de4f489eac731c23ae9ca9cc31\n65536"
      "\n1792947875\n"
      "f125eb4e50c9963ba816380b12bec967a8763cc6da178f856a124f5dc79c37b77d24b930"
      "5dd85e9d9ca707d014425375f04c0f98f687d3ebb32c5c0c8495c8ea\n";
  auto manifest = klip::ParseUpdateManifest(fixture, 1792947874);
  if (!manifest || !klip::VerifyUpdateSignature(*manifest)) return 1;
  auto changed = *manifest;
  changed.payload[20] ^= 1;
  if (klip::VerifyUpdateSignature(changed)) return 2;
  changed = *manifest;
  changed.signature[0] ^= 1;
  if (klip::VerifyUpdateSignature(changed)) return 3;
  const auto path =
      std::filesystem::temp_directory_path() /
      (L"Klip-security-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(GetTickCount64()) + L".bin");
  std::vector<char> bytes(65536, 0);
  {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), bytes.size());
  }
  if (!klip::VerifyUpdateFile(path, *manifest)) return 4;
  {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.put('x');
  }
  if (klip::VerifyUpdateFile(path, *manifest)) return 5;
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.put('x');
  }
  if (klip::VerifyUpdateFile(path, *manifest)) return 6;
  std::filesystem::remove(path);
  if (klip::VerifyUpdateFile(path, *manifest)) return 7;
  klip::Updater updater;
  updater.Initialize(nullptr, path.parent_path());
  if (updater.BeginInstall()) return 8;
  updater.Shutdown();
  std::cout << "Signed manifest, tamper, hash, size, missing file and unready "
               "install checks passed\n";
  return 0;
}
