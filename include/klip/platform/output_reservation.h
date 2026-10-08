#pragma once

#include <Windows.h>
#include <filesystem>

namespace klip {

// A filename reservation is not a writer lock. OBS opens the reserved file itself.
// RemoveEmpty is used only after the output has been released (its writer joined).
// It verifies file identity and denies concurrent writers before deleting, so a
// replacement file or partially written recording is never removed.
class OutputReservation {
 public:
  bool Reserve(const std::filesystem::path& path, DWORD& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    BY_HANDLE_FILE_INFORMATION identity{};
    const bool identified = GetFileInformationByHandle(file, &identity) != FALSE;
    error = identified ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!identified) return false;  // Do not guess ownership or delete by path.
    path_ = path;
    identity_ = identity;
    return true;
  }

  bool RemoveEmpty() noexcept {
    if (path_.empty()) return false;
    HANDLE file = CreateFileW(path_.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION current{};
    bool removed = false;
    if (GetFileInformationByHandle(file, &current) &&
        current.dwVolumeSerialNumber == identity_.dwVolumeSerialNumber &&
        current.nFileIndexHigh == identity_.nFileIndexHigh &&
        current.nFileIndexLow == identity_.nFileIndexLow &&
        CompareFileTime(&current.ftCreationTime, &identity_.ftCreationTime) == 0 &&
        !(current.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        current.nFileSizeHigh == 0 && current.nFileSizeLow == 0) {
      FILE_DISPOSITION_INFO disposition{TRUE};
      removed = SetFileInformationByHandle(file, FileDispositionInfo, &disposition,
                                           sizeof(disposition)) != FALSE;
    }
    CloseHandle(file);
    return removed;
  }

  const std::filesystem::path& Path() const noexcept { return path_; }
  void Forget() noexcept { path_.clear(); identity_ = {}; }

 private:
  std::filesystem::path path_;
  BY_HANDLE_FILE_INFORMATION identity_{};
};

}  // namespace klip
