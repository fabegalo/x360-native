// The one host-path spelling rule of the project (supervisor, launcher,
// extractor and titles). Header-only: titles compile parts of the supervisor
// without linking it. ReXGlue's rex::filesystem::HostPath (SDK patch 0252)
// applies the same rule inside the runtime; tests keep the two in agreement.
//
// Logical paths (stored, compared, displayed, passed to other processes) stay
// in their ordinary spelling. Only the I/O boundary uses Extended(): on Windows
// the \\?\ or \\?\UNC\ form, which Win32 accepts up to 32,767 characters without
// the LongPathsEnabled policy. Never walk the parents of an extended path or run
// weakly_canonical on one: MSVC parses "\\?" as the root name, so its ancestors
// end at "\\?\C:", which Win32 rejects.
#pragma once

#include <filesystem>
#include <string>

namespace x360n::host_path {

inline bool IsExtended(const std::filesystem::path& path) {
#ifdef _WIN32
  return path.native().starts_with(L"\\\\?\\");
#else
  (void)path;
  return false;
#endif
}

// Absolute I/O name. GetFullPathNameW (std::filesystem::absolute) resolves '.',
// '..' and '/' first, because Windows does not normalize extended names.
// Idempotent; device names (\\.\) and the empty path are unchanged; other hosts
// return the path as given.
inline std::filesystem::path Extended(const std::filesystem::path& path) {
#ifdef _WIN32
  if (path.empty() || IsExtended(path) || path.native().starts_with(L"\\\\.\\")) return path;
  std::error_code ec;
  auto name = std::filesystem::absolute(path, ec).lexically_normal().make_preferred().native();
  if (ec || name.empty()) return path;
  return name.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + name.substr(2) : L"\\\\?\\" + name;
#else
  return path;
#endif
}

// The same location without the prefix: drive and UNC names lose \\?\ and
// \\?\UNC\; volume GUID and other extended names are unchanged.
inline std::filesystem::path Ordinary(const std::filesystem::path& path) {
#ifdef _WIN32
  const auto& name = path.native();
  if (name.starts_with(L"\\\\?\\UNC\\")) return std::filesystem::path(L"\\\\" + name.substr(8));
  if (name.starts_with(L"\\\\?\\") && name.size() > 5 && name[5] == L':')
    return std::filesystem::path(name.substr(4));
#endif
  return path;
}

}  // namespace x360n::host_path
