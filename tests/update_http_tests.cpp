#include "klip/update/updater.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
  try {
    if (argc != 3) return 2;
    std::ifstream file(std::filesystem::path(argv[1]), std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    auto manifest = klip::ParseUpdateManifest(text, now);
    if (!manifest || !klip::VerifyUpdateSignature(*manifest)) return 3;
    const std::filesystem::path output = argv[2];
    if (std::filesystem::exists(output)) return 4;
    std::ofstream download(output, std::ios::binary);
    if (!download) return 5;
    const auto suffix = manifest->url.substr(
        std::string("https://klip-ten-zeta.vercel.app").size());
    klip::FetchUpdateHttps(
        std::wstring(suffix.begin(), suffix.end()), manifest->bytes, {},
        [&](const unsigned char* bytes, DWORD count) {
          download.write(reinterpret_cast<const char*>(bytes), count);
          if (!download) throw std::runtime_error("Test write failed");
        });
    download.close();
    if (!klip::VerifyUpdateFile(output, *manifest)) return 6;
    bool rejected = false;
    try {
      klip::FetchUpdateHttps(L"/updates/klip-test-missing-" +
                                 std::to_wstring(GetCurrentProcessId()) +
                                 L".manifest",
                             4096, {}, [](const unsigned char*, DWORD) {});
    } catch (const std::exception& e) {
      rejected = true;
      std::cout << "Missing update resource handled: " << e.what() << '\n';
    }
    if (!rejected) return 7;
    std::stop_source stop;
    bool cancelled = false;
    try {
      klip::FetchUpdateHttps(
          std::wstring(suffix.begin(), suffix.end()), manifest->bytes,
          stop.get_token(),
          [&](const unsigned char*, DWORD) { stop.request_stop(); });
    } catch (const std::exception& e) {
      cancelled = std::string(e.what()) == "Update cancelled";
    }
    if (!cancelled) return 8;
    std::cout << "Native HTTPS download hash/size verified, missing feed "
                 "handled, live download cancellation passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 99;
  }
}
