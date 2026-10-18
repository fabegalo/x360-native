#include "digest_batch.h"
#include "action_trace.h"
#include <unordered_map>
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
std::wstring Extended(std::filesystem::path path) {
  auto name = path.make_preferred().native();
  if (name.starts_with(L"\\\\?\\")) return name;
  return name.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + name.substr(2) : L"\\\\?\\" + name;
}
std::filesystem::path CanonicalName(HANDLE file, const std::filesystem::path& path) {
  std::wstring name(512, L'\0');
  auto count = GetFinalPathNameByHandleW(file, name.data(), static_cast<DWORD>(name.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (count >= name.size()) {
    name.resize(size_t(count) + 1);
    count = GetFinalPathNameByHandleW(file, name.data(), static_cast<DWORD>(name.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  }
  if (count && count < name.size()) {
      name.resize(count);
      if (name.starts_with(L"\\\\?\\UNC\\")) return std::filesystem::path(L"\\\\" + name.substr(8)).lexically_normal();
      if (name.starts_with(L"\\\\?\\") && name.size() > 6 && name[5] == L':')
        return std::filesystem::path(name.substr(4)).lexically_normal();
  }
  return std::filesystem::weakly_canonical(path);
}
#endif
}
struct DigestBatch::State {
  State(DigestBatch* owner_in, State* previous_in, std::function<bool()> cancelled_in)
      : owner(owner_in), previous(previous_in), cancelled(std::move(cancelled_in)) {}
  DigestBatch* owner;
  State* previous;
  std::function<bool()> cancelled;
#ifdef _WIN32
  struct Entry { std::shared_ptr<File> file; FileDigest digest; };
  std::unordered_map<std::string, Entry> entries;
  std::unordered_map<std::filesystem::path, std::string> paths;
  std::unordered_map<std::filesystem::path, std::unique_ptr<File>> directories;
  std::unordered_map<std::filesystem::path, std::filesystem::path> normal;
  std::unordered_map<std::filesystem::path, std::shared_ptr<File>> metadata;
  bool Pin(const std::filesystem::path& directory) {
    if (directories.contains(directory)) return true;
    const auto parent = directory.parent_path();
    if (parent != directory && !Pin(parent)) return false;
    const auto name = Extended(directory);
    auto file = std::make_unique<File>(CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (file->handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file->handle, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    const auto handle = file->handle;
    directories.emplace(directory, std::move(file));
    normal.emplace(directory, CanonicalName(handle, directory));
    return true;
  }
  std::vector<uint8_t> buffer = std::vector<uint8_t>(1 << 20);
#endif
};
DigestBatch::DigestBatch(std::function<bool()> cancelled)
    : state_(std::make_unique<State>(this, current, std::move(cancelled))) { current = state_.get(); }
DigestBatch::~DigestBatch() { current = state_->previous; }
void CheckVerificationCancellation() {
  if (current && current->cancelled && current->cancelled()) throw VerificationCancelled();
}
std::filesystem::path VerificationPath(const std::filesystem::path& path) {
  CheckVerificationCancellation();
#ifdef _WIN32
  if (current) {
    // Exact names already recorded here have ordinary, protected ancestors.
    // Avoid reconstructing and normalizing their components on every hit.
    if (const auto found = current->normal.find(path); found != current->normal.end()) return found->second;
    // Resolve dot components through the filesystem, including link/.. semantics.
    bool ordinary = !path.native().starts_with(L"\\\\?\\");
    for (const auto& part : path) if (part.native() == L"." || part.native() == L"..") ordinary = false;
    if (ordinary) {
      const auto absolute = std::filesystem::absolute(path).lexically_normal();
      if (const auto found = current->normal.find(absolute); found != current->normal.end()) return found->second;
      if (const auto found = current->metadata.find(absolute); found != current->metadata.end())
        return current->normal.emplace(absolute, CanonicalName(found->second->handle, absolute)).first->second;
      if (current->Pin(absolute.parent_path())) {
        const auto attributes = GetFileAttributesW(Extended(absolute).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
          if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (current->Pin(absolute)) return current->normal.at(absolute);
          } else {
            auto file = std::make_shared<File>(CreateFileW(Extended(absolute).c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
            BY_HANDLE_FILE_INFORMATION info{};
            if (file->handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file->handle, &info) &&
                !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
              current->metadata.emplace(absolute, file);
              return current->normal.emplace(absolute, CanonicalName(file->handle, absolute)).first->second;
            }
          }
        }
      }
    }
  }
#endif
  return std::filesystem::weakly_canonical(path);
}
bool HasProtectedVerificationPath(const std::filesystem::path& path) {
#ifdef _WIN32
  if (!current || path.native().starts_with(L"\\\\?\\")) return false;
  if (current->metadata.contains(path) || current->directories.contains(path)) return true;
  for (const auto& part : path) if (part.native() == L"." || part.native() == L"..") return false;
  const auto absolute = (path.is_absolute() ? path : std::filesystem::absolute(path)).lexically_normal();
  return current->metadata.contains(absolute) || current->directories.contains(absolute);
#else
  (void)path; return false;
#endif
}

std::optional<FileDigest> TryBatchDigest(const std::filesystem::path& path, bool& handled) {
  CheckVerificationCancellation();
#ifdef _WIN32
  handled = current != nullptr;
  if (!handled) return std::nullopt;
  if (const auto found = current->paths.find(path); found != current->paths.end()) {
    ++current->owner->hits;
    return current->entries.at(found->second).digest;
  }
  std::error_code path_error;
  const auto absolute = std::filesystem::absolute(path, path_error).lexically_normal();
  if (path_error) return std::nullopt;
  if (const auto found = current->paths.find(absolute); found != current->paths.end()) {
    ++current->owner->hits;
    return current->entries.at(found->second).digest;
  }
  bool pinned = !absolute.native().starts_with(L"\\\\?\\") && current->Pin(absolute.parent_path());
  auto name = Extended(absolute);
  std::shared_ptr<File> file;
  if (const auto found = current->metadata.find(absolute); found != current->metadata.end()) file = found->second;
  else file = std::make_shared<File>(CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | (pinned ? FILE_FLAG_OPEN_REPARSE_POINT : 0), nullptr));
  if (file->handle == INVALID_HANDLE_VALUE) return std::nullopt;
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file->handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return std::nullopt;
  if (pinned && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    pinned = false;
    file = std::make_shared<File>(CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file->handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file->handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return std::nullopt;
  }
  const auto key = std::to_string(info.dwVolumeSerialNumber) + ":" +
      std::to_string(info.nFileIndexHigh) + ":" + std::to_string(info.nFileIndexLow);
  const bool reusable = info.nFileIndexHigh || info.nFileIndexLow;
  if (pinned) {
    current->metadata.emplace(absolute, file);
  }
  if (const auto it = current->entries.find(key); reusable && it != current->entries.end()) {
    if (pinned) current->paths.emplace(absolute, key);
    ++current->owner->hits;
    return it->second.digest;
  }
  ActionTrace trace("hash.stream");
  LARGE_INTEGER beginning{};
  if (!SetFilePointerEx(file->handle, beginning, nullptr, FILE_BEGIN)) return std::nullopt;
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
  if (pinned && reusable) current->paths.emplace(absolute, key);
  return result;
#else
  (void)path; handled = false; return std::nullopt;
#endif
}
}
