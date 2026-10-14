#pragma once
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace x360n {
struct DashboardAsset {
  std::string_view path;
  std::string_view sha256;
};
// Per-launch check; no basename search, alternate roots or process-global cache.
// Empty hash means the matching dependency is not yet identified, never a wildcard.
std::string VerifyDashboardAssets(const std::filesystem::path& root,
                                 std::string_view version,
                                 std::span<const DashboardAsset> assets);
std::filesystem::path DashboardCacheRoot(const std::filesystem::path& root,
                                        std::string_view version);
}
