#pragma once
// Language of the shared launcher. One set of strings for every system.
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace x360n::desktop {
inline bool english = false;
inline std::string Tr(const char* pt, const char* en) { return english ? en : pt; }
// Channel named by the prerelease tag of the version CMake derives from the
// commit history (cmake/distribution/x360n_version.cmake). A "dev" identifier
// wins over any other tag; an unknown tag is a pre-release, never stable.
inline std::string VersionChannel(std::string_view version) {
  const auto dash = version.find('-');
  if (dash == std::string_view::npos) return Tr("Estável", "Stable");
  std::vector<std::string_view> ids;
  for (size_t at = dash + 1; at <= version.size();) {
    const auto end = std::min(version.find_first_of(".-", at), version.size());
    ids.push_back(version.substr(at, end - at)); at = end + 1;
  }
  if (std::find(ids.begin(), ids.end(), "dev") != ids.end()) return Tr("Desenvolvimento", "Development");
  if (ids.front() == "rc") return "Release Candidate";
  if (ids.front() == "preview" || ids.front() == "beta" || ids.front() == "alpha") return Tr("Prévia", "Preview");
  return Tr("Pré-lançamento", "Pre-release");
}
inline std::string VersionLabel(std::string_view version) { return "v" + std::string(version) + " · " + VersionChannel(version); }
// The system and architecture this launcher was built for.
#if defined(_WIN32) && defined(_M_ARM64)
inline constexpr const char* kPlatform = "Windows ARM64";
#elif defined(_WIN32)
inline constexpr const char* kPlatform = "Windows x64";
#elif defined(__APPLE__) && defined(__aarch64__)
inline constexpr const char* kPlatform = "macOS ARM64";
#elif defined(__APPLE__)
inline constexpr const char* kPlatform = "macOS x64";
#elif defined(__aarch64__)
inline constexpr const char* kPlatform = "Linux ARM64";
#else
inline constexpr const char* kPlatform = "Linux x64";
#endif
}  // namespace x360n::desktop
