#include "klip/update/updater.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <functional>
#include <stdexcept>

#include "klip/update/public_key.h"

namespace klip {
namespace {
struct Internet {
  HINTERNET value = nullptr;
  ~Internet() {
    if (value) WinHttpCloseHandle(value);
  }
};
struct DownloadFile {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~DownloadFile() {
    if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
  }
};
struct Hash {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::vector<unsigned char> object;
  Hash() {
    try {
      DWORD count = 0, size = 0;
      if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                      0) < 0 ||
          BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                            reinterpret_cast<PUCHAR>(&size), sizeof(size),
                            &count, 0) < 0)
        throw std::runtime_error("SHA-256 unavailable");
      object.resize(size);
      if (BCryptCreateHash(alg, &hash, object.data(), size, nullptr, 0, 0) < 0)
        throw std::runtime_error("SHA-256 initialization failed");
    } catch (...) {
      if (hash) BCryptDestroyHash(hash);
      if (alg) BCryptCloseAlgorithmProvider(alg, 0);
      throw;
    }
  }
  ~Hash() {
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  }
  void Add(const unsigned char* data, std::size_t size) {
    if (BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size),
                       0) < 0)
      throw std::runtime_error("SHA-256 failed");
  }
  std::array<unsigned char, 32> Finish() {
    std::array<unsigned char, 32> value{};
    if (BCryptFinishHash(hash, value.data(), 32, 0) < 0)
      throw std::runtime_error("SHA-256 failed");
    return value;
  }
};
std::uint64_t Now() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}
// Requests are pinned to one HTTPS origin; redirects and executable protocols
// are rejected.
void Fetch(const std::wstring& path, std::uint64_t limit,
           const std::stop_token& stop,
           const std::function<void(const unsigned char*, DWORD)>& receive) {
  Internet session{WinHttpOpen(L"Klip updater/1",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr,
                               nullptr, 0)};
  if (!session.value)
    throw std::runtime_error("Cannot initialize update connection");
  WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000);
  Internet connection{WinHttpConnect(session.value, L"klip-ten-zeta.vercel.app",
                                     INTERNET_DEFAULT_HTTPS_PORT, 0)};
  Internet request{WinHttpOpenRequest(
      connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
  DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (!request.value ||
      !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                        sizeof(policy)) ||
      !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.value, nullptr))
    throw std::runtime_error("Could not reach update server");
  DWORD status = 0, size = sizeof(status);
  if (!WinHttpQueryHeaders(
          request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          nullptr, &status, &size, nullptr) ||
      status != 200)
    throw std::runtime_error(
        status == 404 ? (path == L"/updates/windows.manifest"
                             ? "No signed update has been published yet."
                             : "The update file is unavailable. Check again "
                               "later or use the website installer.")
                      : "Update server rejected the request");
  std::array<unsigned char, 65536> buffer{};
  std::uint64_t total = 0;
  DWORD read = 0;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (!stop.stop_requested()) {
    if (std::chrono::steady_clock::now() > deadline)
      throw std::runtime_error("Update download timed out");
    if (!WinHttpReadData(request.value, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &read))
      throw std::runtime_error("Update download interrupted");
    if (!read) return;
    total += read;
    if (total > limit)
      throw std::runtime_error("Update response exceeded its size limit");
    receive(buffer.data(), read);
  }
  throw std::runtime_error("Update cancelled");
}
}  // namespace
void FetchUpdateHttps(const std::wstring& path, std::uint64_t limit,
                      std::stop_token stop, const Updater::Receive& receive) {
  Fetch(path, limit, stop, receive);
}
std::filesystem::path CurrentExecutable() {
  std::wstring text(32768, L'\0');
  auto n =
      GetModuleFileNameW(nullptr, text.data(), static_cast<DWORD>(text.size()));
  if (!n || n >= text.size()) return {};
  text.resize(n);
  return text;
}
std::filesystem::path InstalledKlipRoot() {
  std::wstring root(32768, L'\0');
  DWORD bytes = static_cast<DWORD>(root.size() * sizeof(wchar_t));
  if (RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{"
                   L"8A44B102-4E2D-4701-B01E-0B43D5C6C5A8}_is1",
                   L"InstallLocation", RRF_RT_REG_SZ, nullptr, root.data(),
                   &bytes) != ERROR_SUCCESS)
    return {};
  root.resize(wcslen(root.c_str()));
  return root;
}
bool VerifyUpdateSignature(const UpdateManifest& manifest) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_KEY_HANDLE key = nullptr;
  bool verified = false;
  try {
    auto blob = DecodeHex(kUpdatePublicKey);
    Hash hash;
    hash.Add(reinterpret_cast<const unsigned char*>(manifest.payload.data()),
             manifest.payload.size());
    auto digest = hash.Finish();
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr,
                                    0) >= 0 &&
        BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                            blob.data(), static_cast<ULONG>(blob.size()),
                            0) >= 0)
      verified = BCryptVerifySignature(
                     key, nullptr, digest.data(), 32,
                     const_cast<PUCHAR>(manifest.signature.data()),
                     static_cast<ULONG>(manifest.signature.size()), 0) >= 0;
  } catch (...) {
    verified = false;
  }
  if (key) BCryptDestroyKey(key);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  return verified;
}
bool VerifyUpdateFile(const std::filesystem::path& path,
                      const UpdateManifest& manifest) {
  try {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    Hash hash;
    std::array<unsigned char, 65536> buffer{};
    std::uint64_t total = 0;
    while (input) {
      input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
      const auto count = input.gcount();
      total += static_cast<std::uint64_t>(count);
      if (total > manifest.bytes) return false;
      hash.Add(buffer.data(), static_cast<std::size_t>(count));
    }
    if (!input.eof() || total != manifest.bytes) return false;
    const auto digest = hash.Finish();
    const auto expected = DecodeHex(manifest.sha256);
    return std::equal(digest.begin(), digest.end(), expected.begin(),
                      expected.end());
  } catch (...) {
    return false;
  }
}
void Updater::Initialize(HWND window, const std::filesystem::path& cache) {
  window_ = window;
  cache_ = cache;
  const auto root = InstalledKlipRoot();
  std::error_code e;
  view_.installed = !root.empty() &&
                    std::filesystem::equivalent(
                        CurrentExecutable(), root / L"bin/64bit/Klip.exe", e) &&
                    !e;
}
void Updater::Publish(UpdateView view) {
  {
    std::lock_guard lock(mutex_);
    view_ = std::move(view);
  }
  if (window_) PostMessageW(window_, WM_APP + 57, 0, 0);
}
void Updater::FetchUpdate(const std::wstring& path, std::uint64_t limit,
                          std::stop_token stop, const Receive& receive) {
  if (transport_)
    transport_(path, limit, stop, receive);
  else
    FetchUpdateHttps(path, limit, stop, receive);
}
UpdateView Updater::Snapshot() const {
  std::lock_guard lock(mutex_);
  return view_;
}
void Updater::Check() {
  auto view = Snapshot();
  if (view.phase == UpdatePhase::kChecking ||
      view.phase == UpdatePhase::kDownloading ||
      view.phase == UpdatePhase::kStarting)
    return;
  if (worker_.joinable()) worker_.join();
  view.phase = UpdatePhase::kChecking;
  view.message = "Checking signed releases...";
  Publish(view);
  worker_ = std::jthread([this, view](std::stop_token stop) mutable {
    try {
      std::string text;
      FetchUpdate(L"/updates/windows.manifest", 4096, stop,
                  [&](const unsigned char* data, DWORD n) {
                    text.append(reinterpret_cast<const char*>(data), n);
                  });
      auto parsed = ParseUpdateManifest(text, Now());
      if (!parsed || !VerifyUpdateSignature(*parsed))
        throw std::runtime_error(
            "Update signature or manifest is invalid. Nothing was installed.");
      manifest_ = std::move(parsed);
      view.version = manifest_->version;
      view.phase = NewerVersion(view.version, KLIP_APP_VERSION)
                       ? UpdatePhase::kAvailable
                       : UpdatePhase::kCurrent;
      view.message = view.phase == UpdatePhase::kAvailable
                         ? "A verified update is available."
                         : "You are up to date.";
      Publish(view);
    } catch (const std::exception& e) {
      view.phase = UpdatePhase::kError;
      view.message = e.what();
      Publish(view);
    }
  });
}
void Updater::Download() {
  auto view = Snapshot();
  if (view.phase != UpdatePhase::kAvailable || !view.installed) return;
  if (worker_.joinable()) worker_.join();
  if (!manifest_) return;
  auto manifest = *manifest_;
  view.phase = UpdatePhase::kDownloading;
  view.message = "Downloading update...";
  view.progress = 0;
  Publish(view);
  worker_ = std::jthread([this, view, manifest](std::stop_token stop) mutable {
    std::filesystem::path partial;
    bool owns_partial = false;
    try {
      std::filesystem::create_directories(cache_);
      installer_ = cache_ / (L"Klip-update-" +
                             std::to_wstring(GetTickCount64()) + L".exe");
      partial = installer_;
      partial += L".partial";
      DownloadFile file;
      file.value =
          CreateFileW(partial.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file.value == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create update cache file");
      owns_partial = true;
      std::uint64_t received = 0;
      const auto path = manifest.url.substr(
          std::string("https://klip-ten-zeta.vercel.app").size());
      FetchUpdate(std::wstring(path.begin(), path.end()), manifest.bytes, stop,
                  [&](const unsigned char* data, DWORD n) {
                    DWORD written = 0;
                    if (!WriteFile(file.value, data, n, &written, nullptr) ||
                        written != n)
                      throw std::runtime_error(
                          "Insufficient space or update write failed");
                    received += n;
                    view.progress = static_cast<float>(received) /
                                    static_cast<float>(manifest.bytes);
                    Publish(view);
                  });
      if (!FlushFileBuffers(file.value))
        throw std::runtime_error("Update cache could not be finalized");
      CloseHandle(file.value);
      file.value = INVALID_HANDLE_VALUE;
      if (!VerifyUpdateFile(partial, manifest))
        throw std::runtime_error("Downloaded update failed integrity checks");
      if (stop.stop_requested()) throw std::runtime_error("Update cancelled");
      if (!MoveFileW(partial.c_str(), installer_.c_str()))
        throw std::runtime_error("Cannot publish verified update cache file");
      std::ofstream signed_file(
          std::filesystem::path(installer_.wstring() + L".manifest"),
          std::ios::binary);
      signed_file << manifest.payload;
      constexpr char hex[] = "0123456789abcdef";
      for (auto b : manifest.signature)
        signed_file << hex[b >> 4] << hex[b & 15];
      signed_file << '\n';
      signed_file.close();
      if (!signed_file)
        throw std::runtime_error("Cannot store signed update manifest");
      view.phase = UpdatePhase::kReady;
      view.message = "Update verified. Restart when you finish capturing.";
      Publish(view);
    } catch (const std::exception& e) {
      std::error_code ignored;
      if (owns_partial && !partial.empty())
        std::filesystem::remove(partial, ignored);
      view.phase = UpdatePhase::kError;
      view.message = e.what();
      Publish(view);
    }
  });
}
void Updater::Cancel() {
  if (worker_.joinable()) worker_.request_stop();
}
bool Updater::BeginInstall() {
  auto view = Snapshot();
  if (view.phase != UpdatePhase::kReady || !view.installed) return false;
  if (worker_.joinable()) worker_.join();
  try {
    const auto helper_source =
        CurrentExecutable().parent_path() / L"KlipUpdater.exe";
    const auto helper_copy =
        cache_ /
        (L"KlipUpdater-" + std::to_wstring(GetTickCount64()) + L".exe");
    std::filesystem::copy_file(helper_source, helper_copy);
    const auto name = L"Local\\Klip.Update.Ready." +
                      std::to_wstring(GetCurrentProcessId()) + L"." +
                      std::to_wstring(GetTickCount64());
    ready_ = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    if (!ready_)
      throw std::runtime_error("Cannot start updater synchronization");
    std::wstring args = L"\"" + helper_copy.wstring() + L"\" " +
                        std::to_wstring(GetCurrentProcessId()) + L" \"" + name +
                        L"\" \"" + installer_.wstring() + L"\"";
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(helper_copy.c_str(), args.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, cache_.c_str(),
                        &start, &process))
      throw std::runtime_error("Cannot launch verified update helper");
    CloseHandle(process.hThread);
    helper_ = process.hProcess;
    view.phase = UpdatePhase::kStarting;
    view.message = "Preparing update restart...";
    Publish(view);
    return true;
  } catch (const std::exception& e) {
    if (ready_) {
      CloseHandle(ready_);
      ready_ = nullptr;
    }
    view.phase = UpdatePhase::kError;
    view.message = e.what();
    Publish(view);
    return false;
  }
}
bool Updater::InstallationReady() {
  if (!ready_) return false;
  if (WaitForSingleObject(ready_, 0) == WAIT_OBJECT_0) return true;
  if (helper_ && WaitForSingleObject(helper_, 0) == WAIT_OBJECT_0) {
    auto view = Snapshot();
    view.phase = UpdatePhase::kError;
    view.message =
        "Update helper rejected the installation. Klip was not closed.";
    Publish(view);
    CloseHandle(ready_);
    ready_ = nullptr;
    CloseHandle(helper_);
    helper_ = nullptr;
  }
  return false;
}
void Updater::Shutdown() {
  Cancel();
  if (worker_.joinable()) worker_.join();
  if (ready_) {
    CloseHandle(ready_);
    ready_ = nullptr;
  }
  if (helper_) {
    CloseHandle(helper_);
    helper_ = nullptr;
  }
  window_ = nullptr;
}
}  // namespace klip
