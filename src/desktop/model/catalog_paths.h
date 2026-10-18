#pragma once
#include "catalog.h"
#include <map>
#include <memory>
#include <optional>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace x360n::desktop {
// ReadCatalog often visits thousands of siblings. Pin ordinary directories for
// this read only, excluding rename/reparse updates, and inspect each leaf once.
// Links, missing parents and unsupported hosts retain the original full checks.
class CatalogPaths {
 public:
  explicit CatalogPaths(std::filesystem::path root) : root_(std::filesystem::weakly_canonical(root)) {}
  std::optional<std::filesystem::path> Resolve(const std::filesystem::path& file) {
#ifdef _WIN32
    auto absolute = std::filesystem::absolute(file).lexically_normal();
    const auto relative = absolute.lexically_relative(root_);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") return {};
    if (!Pin(absolute.parent_path())) return {};
    const auto name = Extended(absolute);
    const auto attributes = GetFileAttributesW(name.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      if (GetLastError() != ERROR_FILE_NOT_FOUND) return {};
    } else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return {};
    return absolute;
#else
    (void)file; return {};
#endif
  }
 private:
  std::filesystem::path root_;
#ifdef _WIN32
  struct Directory {
    HANDLE handle;
    explicit Directory(HANDLE value) : handle(value) {}
    ~Directory() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
  };
  std::map<std::filesystem::path, std::unique_ptr<Directory>> directories_;
  static std::wstring Extended(std::filesystem::path path) {
    auto name = path.make_preferred().native();
    if (name.starts_with(L"\\\\?\\")) return name;
    return name.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + name.substr(2) : L"\\\\?\\" + name;
  }
  bool Pin(const std::filesystem::path& directory) {
    if (directories_.contains(directory)) return true;
    // Pin the ancestors of the root as well: renaming a parent must not move
    // the trusted directory tree out from under this read.
    const auto parent = directory.parent_path();
    if (parent != directory && !Pin(parent)) return false;
    const auto name = Extended(directory);
    auto handle = std::make_unique<Directory>(CreateFileW(name.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle->handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle->handle, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    directories_.emplace(directory, std::move(handle));
    return true;
  }
#endif
};
}
