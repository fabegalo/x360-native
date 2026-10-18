#include "digest_batch.h"
#include "action_trace.h"
#include <map>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <bcrypt.h>
#endif

namespace x360n {
namespace {
thread_local DigestBatch::State* current = nullptr;
#ifdef _WIN32
struct File {
  HANDLE handle;
  explicit File(HANDLE value) : handle(value) {}
  ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
#endif
}
struct DigestBatch::State {
  State(DigestBatch* owner_in, State* previous_in, std::function<bool()> cancelled_in)
      : owner(owner_in), previous(previous_in), cancelled(std::move(cancelled_in)) {}
  DigestBatch* owner;
  State* previous;
  std::function<bool()> cancelled;
#ifdef _WIN32
  struct Entry { std::unique_ptr<File> file; FileDigest digest; };
  std::map<std::string, Entry> entries;
  std::vector<uint8_t> buffer = std::vector<uint8_t>(1 << 20);
#endif
};
DigestBatch::DigestBatch(std::function<bool()> cancelled)
    : state_(std::make_unique<State>(this, current, std::move(cancelled))) { current = state_.get(); }
DigestBatch::~DigestBatch() { current = state_->previous; }
void CheckVerificationCancellation() {
  if (current && current->cancelled && current->cancelled()) throw VerificationCancelled();
}

std::optional<FileDigest> TryBatchDigest(const std::filesystem::path& path, bool& handled) {
  CheckVerificationCancellation();
#ifdef _WIN32
  handled = current != nullptr;
  if (!handled) return std::nullopt;
  std::error_code path_error;
  auto name = std::filesystem::absolute(path, path_error).lexically_normal().make_preferred().native();
  if (path_error) return std::nullopt;
  if (!name.starts_with(L"\\\\?\\")) name = name.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + name.substr(2) : L"\\\\?\\" + name;
  auto file = std::make_unique<File>(CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
  if (file->handle == INVALID_HANDLE_VALUE) return std::nullopt;
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file->handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return std::nullopt;
  const auto key = std::to_string(info.dwVolumeSerialNumber) + ":" +
      std::to_string(info.nFileIndexHigh) + ":" + std::to_string(info.nFileIndexLow);
  const bool reusable = info.nFileIndexHigh || info.nFileIndexLow;
  if (const auto it = current->entries.find(key); reusable && it != current->entries.end()) {
    ++current->owner->hits;
    return it->second.digest;
  }
  ActionTrace trace("hash.stream");
  struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    Algorithm() { if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) handle = nullptr; }
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
  };
  static const Algorithm algorithm;
  struct Hash { BCRYPT_HASH_HANDLE handle = nullptr; ~Hash() { if (handle) BCryptDestroyHash(handle); } } hash;
  if (!algorithm.handle || BCryptCreateHash(algorithm.handle, &hash.handle, nullptr, 0, nullptr, 0, 0) < 0) return std::nullopt;
  uint64_t size = 0;
  for (;;) {
    CheckVerificationCancellation();
    DWORD count = 0;
    if (!ReadFile(file->handle, current->buffer.data(), static_cast<DWORD>(current->buffer.size()), &count, nullptr)) return std::nullopt;
    if (!count) break;
    if (BCryptHashData(hash.handle, current->buffer.data(), count, 0) < 0) return std::nullopt;
    size += count;
  }
  std::array<uint8_t, 32> digest{};
  if (BCryptFinishHash(hash.handle, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return std::nullopt;
  FileDigest result{size, ToHex(digest)};
  ++current->owner->files; current->owner->bytes += size;
  if (reusable) current->entries.emplace(key, DigestBatch::State::Entry{std::move(file), result});
  return result;
#else
  (void)path; handled = false; return std::nullopt;
#endif
}
}
