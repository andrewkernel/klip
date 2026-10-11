#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "klip/clips/trimmer.h"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  klip::ClipTrimmer trim;
  const auto wait = [&] {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (trim.Snapshot().busy && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return !trim.Snapshot().busy;
  };
  const auto text = std::string(argv[1]);
  const auto source = std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t*>(text.data()), text.size()));
  const auto bytes = std::filesystem::file_size(source);
  if (!trim.Inspect(source) || !wait() || !trim.Snapshot().loaded) return 3;
  const auto view = trim.Snapshot();
  std::cout << "duration=" << view.duration
            << " keyframes=" << view.keyframes.size() << '\n';
  if (!trim.Preview(5.2) || !wait() || !trim.Snapshot().image) return 4;
  if (trim.Export(7, 3) || trim.Export(-1, 3) ||
      trim.Export(0, view.duration + 1))
    return 5;
  if (!trim.Export(5.2, 11.3) || !wait()) return 6;
  const auto result = trim.Snapshot();
  if (result.output.empty() ||
      !std::filesystem::is_regular_file(result.output) ||
      std::filesystem::file_size(source) != bytes ||
      result.actual_start > 5.201 || result.actual_end < 11.29 ||
      result.actual_end - result.actual_start > 11)
    return 7;
  std::cout << "start=" << result.actual_start << " end=" << result.actual_end
            << " output=" << result.output.string() << '\n';
  // Cancellation is bounded and does not replace the original or a finished
  // copy.
  if (!trim.Preview(1)) return 8;
  trim.Cancel();
  if (!wait()) return 9;
  if (std::filesystem::file_size(source) != bytes) return 10;
  // An existing partial belongs to someone else; a failed reservation must
  // not delete it. This fixture is created only beside an isolated test copy.
  std::filesystem::path next;
  for (unsigned i = 1; i <= 10000; ++i) {
    next = source.parent_path() / (source.stem().wstring() + L"-trimmed-" +
                                   std::to_wstring(i) + L".mkv");
    if (!std::filesystem::exists(next)) break;
  }
  auto occupied = next;
  occupied += L"." + std::to_wstring(GetCurrentProcessId()) + L".partial";
  {
    std::ofstream file(occupied);
    file << "owned test sentinel";
  }
  if (!trim.Export(5.2, 11.3) || !wait() || !trim.Snapshot().output.empty() ||
      !std::filesystem::exists(occupied))
    return 11;
  {
    std::ifstream file(occupied);
    std::string text;
    std::getline(file, text);
    if (text != "owned test sentinel") return 12;
  }
  std::filesystem::remove(occupied);
  HANDLE writing =
      CreateFileW(source.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (writing == INVALID_HANDLE_VALUE) return 13;
  if (!trim.Inspect(source) || !wait() || trim.Snapshot().loaded) {
    CloseHandle(writing);
    return 14;
  }
  CloseHandle(writing);
  if (std::filesystem::file_size(source) != bytes) return 15;
  trim.Shutdown();
  std::cout << "Trim integration passed\n";
  return 0;
}
