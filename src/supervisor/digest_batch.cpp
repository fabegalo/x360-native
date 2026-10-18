#include "digest_batch.h"
#include "action_trace.h"
#include <unordered_map>
#include <stdexcept>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <unordered_set>
#include <algorithm>
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
std::optional<FileDigest> HashHeldFile(HANDLE file, std::vector<uint8_t>& buffer,
                                     const std::function<bool()>& keep_reading) {
  ActionTrace trace("hash.stream");
  LARGE_INTEGER beginning{};
  if (!SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN)) return std::nullopt;
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
    if (!keep_reading()) return std::nullopt;
    DWORD count = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) return std::nullopt;
    if (!count) break;
    if (BCryptHashData(hash.handle, buffer.data(), count, 0) < 0) return std::nullopt;
    size += count;
  }
  std::array<uint8_t, 32> digest{};
  if (BCryptFinishHash(hash.handle, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return std::nullopt;
  return FileDigest{size, ToHex(digest)};
}
#endif
}
struct DigestBatch::State {
  // A nested validation must not mask cancellation of its enclosing operation.
  State(DigestBatch* owner_in, State* previous_in, std::function<bool()> cancelled_in)
      : owner(owner_in), previous(previous_in),
        cancelled(cancelled_in ? std::move(cancelled_in) : (previous_in ? previous_in->cancelled : std::function<bool()>{})) {}
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
  struct Pending {
    std::filesystem::path path;
    std::string key;
    std::shared_ptr<File> file;
    std::optional<FileDigest> digest;
  };
  bool collecting = false;
  std::vector<Pending> pending;
  std::unordered_set<std::string> pending_keys;
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
  if (current->collecting && pinned && reusable &&
      ((uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow) >= (1u << 20)) {
    if (current->pending_keys.insert(key).second)
      current->pending.push_back({absolute, key, std::move(file), {}});
    return std::nullopt;
  }
  const auto result = HashHeldFile(file->handle, current->buffer, [] {
    CheckVerificationCancellation(); return true;
  });
  if (!result) return std::nullopt;
  ++current->owner->files; current->owner->bytes += result->size;
  if (reusable) current->entries.emplace(key, DigestBatch::State::Entry{std::move(file), *result});
  if (pinned && reusable) current->paths.emplace(absolute, key);
  return result;
#else
  (void)path; handled = false; return std::nullopt;
#endif
}
void PrimeVerificationDigests(std::span<const std::filesystem::path> paths, unsigned workers) {
  CheckVerificationCancellation();
#ifdef _WIN32
  if (!current || paths.empty()) return;
  auto* state = current;
  struct Reset {
    DigestBatch::State* state;
    ~Reset() { state->collecting = false; state->pending.clear(); state->pending_keys.clear(); }
  } reset{state};
  state->collecting = true;
  for (const auto& path : paths) { bool handled = false; TryBatchDigest(path, handled); }
  state->collecting = false;
  if (state->pending.empty()) return;
  const unsigned count = static_cast<unsigned>(std::min<size_t>(state->pending.size(), std::clamp(workers, 1u, 4u)));
  std::atomic<size_t> next{0};
  std::atomic<unsigned> done{0};
  std::atomic<bool> stopped{false};
  std::mutex mutex;
  std::condition_variable progress;
  // Workers never touch thread-local verification state or invoke the owner's
  // cancellation/UI callback. The owner polls it while workers stream 1 MiB blocks.
  std::vector<std::jthread> threads;
  try {
    for (unsigned i = 0; i < count; ++i) threads.emplace_back([&] {
      try {
        std::vector<uint8_t> buffer(1 << 20);
        while (!stopped.load(std::memory_order_relaxed)) {
          const auto index = next.fetch_add(1, std::memory_order_relaxed);
          if (index >= state->pending.size()) break;
          auto& job = state->pending[index];
          job.digest = HashHeldFile(job.file->handle, buffer, [&] { return !stopped.load(std::memory_order_relaxed); });
        }
      } catch (...) { stopped.store(true, std::memory_order_relaxed); }
      ++done; progress.notify_one();
    });
    while (done.load() != count) {
      CheckVerificationCancellation();
      std::unique_lock lock(mutex);
      progress.wait_for(lock, std::chrono::milliseconds(10), [&] { return done.load() == count; });
    }
  } catch (...) { stopped.store(true); throw; }
  threads.clear(); // Join before publishing results or releasing held files.
  CheckVerificationCancellation();
  for (auto& job : state->pending) if (job.digest) {
    ++state->owner->files; state->owner->bytes += job.digest->size;
    state->entries.emplace(job.key, DigestBatch::State::Entry{std::move(job.file), *job.digest});
    state->paths.emplace(job.path, job.key);
  }
#else
  (void)paths; (void)workers;
#endif
}
}
