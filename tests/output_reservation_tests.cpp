#include "klip/platform/output_reservation.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  wchar_t temporary[MAX_PATH]{};
  if (!GetTempPathW(MAX_PATH, temporary)) return 1;
  const auto root = std::filesystem::path(temporary) /
      ("klip-reservation-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
  std::filesystem::create_directory(root);
  std::vector<std::filesystem::path> owned;
  int result = 0;
  try {
    DWORD error = 0;
    const auto empty = root / L"empty.mkv"; owned.push_back(empty);
    klip::OutputReservation reservation;
    Check(reservation.Reserve(empty, error), "Could not reserve fresh filename");
    klip::OutputReservation collision;
    Check(!collision.Reserve(empty, error) && (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS),
          "An existing filename must never be overwritten");
    Check(reservation.RemoveEmpty() && !std::filesystem::exists(empty), "Owned empty reservation was not removed");

    const auto partial = root / L"partial.mkv"; owned.push_back(partial);
    Check(reservation.Reserve(partial, error), "Could not reserve partial filename");
    HANDLE writer = CreateFileW(partial.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(writer != INVALID_HANDLE_VALUE, "Could not open simulated mux writer");
    const bool removed_while_open = reservation.RemoveEmpty();
    DWORD written = 0;
    const bool wrote = WriteFile(writer, "media", 5, &written, nullptr) != FALSE;
    CloseHandle(writer);
    Check(!removed_while_open, "Must not delete a reservation while a writer owns it");
    Check(wrote && written == 5, "Could not write simulated partial media");
    Check(!reservation.RemoveEmpty() && std::filesystem::file_size(partial) == 5,
          "Partial media must be preserved after a failed save");

    const auto replaced = root / L"replaced.mkv"; owned.push_back(replaced);
    const auto original = root / L"original.mkv"; owned.push_back(original);
    Check(reservation.Reserve(replaced, error), "Could not reserve replacement filename");
    // Move the original away rather than deleting it: this prevents file-ID reuse.
    Check(MoveFileW(replaced.c_str(), original.c_str()) != FALSE, "Could not move original reservation");
    Check(collision.Reserve(replaced, error), "Could not create replacement file");
    Check(!reservation.RemoveEmpty() && std::filesystem::exists(replaced),
          "Must not remove a different file that now occupies the reserved name");
    Check(collision.RemoveEmpty(), "Replacement owner could not remove its own empty file");
    std::cout << "Output reservations: collisions, writer ownership, partial media and file identity passed\n";
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n'; result = 1;
  }
  for (const auto& path : owned) { std::error_code ignored; std::filesystem::remove(path, ignored); }
  std::error_code ignored; std::filesystem::remove(root, ignored);
  return result;
}
