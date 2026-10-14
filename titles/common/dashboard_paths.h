#pragma once
#include "dashboard_assets.h"
#include <rex/rex_app.h>
#include <rex/logging.h>
#include <rex/system.h>

namespace x360n {
// Called before constructing Runtime or loading any original image.
inline bool PrepareDashboardPaths(rex::PathConfig& paths, std::string_view version,
                                  std::span<const DashboardAsset> assets) {
  auto error = paths.update_data_root.empty() ? VerifyDashboardAssets(paths.game_data_root, version, assets)
      : "Dashboard Retail " + std::string(version) + ": update_data_root overlays are not supported; use the complete matching original package";
  if (!error.empty()) {
    REXLOG_ERROR("{}", error);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, error);
    return false;
  }
  paths.cache_root = DashboardCacheRoot(paths.cache_root.empty() ? paths.user_data_root / "cache" : paths.cache_root, version);
  // An explicit root also disables the SDK's parent-directory metadata search.
  paths.metadata_root = paths.metadata_root.empty() ? paths.game_data_root / "metadata"
      : DashboardCacheRoot(paths.metadata_root, version);
  REXLOG_INFO("Dashboard Retail {}: verified {} original dependencies in {}; no cross-version fallback",
              version, assets.size(), paths.game_data_root.string());
  return true;
}
}
