#include "dashboard_assets.h"
#include "../../src/supervisor/sha256.h"

namespace x360n {
std::string VerifyDashboardAssets(const std::filesystem::path& root,
                                 std::string_view version,
                                 std::span<const DashboardAsset> assets) {
  namespace fs = std::filesystem;
  const auto prefix = "Dashboard Retail " + std::string(version) + ": ";
  std::error_code ec;
  const auto base = fs::canonical(root, ec);
  if (ec || !fs::is_directory(base)) return prefix + "original content directory is missing: " + root.string();
  if (assets.empty()) return prefix + "no original dependencies registered";
  for (const auto& asset : assets) {
    const auto label = prefix + "required original dependency " + std::string(asset.path);
    if (asset.sha256.size() != 64) return label + " has no verified version/hash; supply the matching version (no fallback)";
    const auto path = fs::canonical(base / asset.path, ec);
    if (ec) return label + " is missing or inaccessible; expected SHA-256 " + std::string(asset.sha256);
    const auto relative = path.lexically_relative(base);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
      return label + " resolves outside this dashboard's content directory (no shared system modules)";
    const auto digest = DigestFile(path);
    if (!digest) return label + " cannot be read: " + path.string();
    if (digest->sha256 != asset.sha256)
      return label + " has incompatible SHA-256 " + digest->sha256 + "; expected " + std::string(asset.sha256) + " (no fallback)";
  }
  return {};
}
std::filesystem::path DashboardCacheRoot(const std::filesystem::path& root,
                                        std::string_view version) {
  return root / ("dashboard-" + std::string(version));
}
}
