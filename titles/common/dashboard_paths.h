#pragma once
#include "dashboard_assets.h"
#include <rex/kernel/title_paths.h>
#include <rex/path_settings.h>
#include <rex/logging/log_category.h>
#include <rex/result.h>
#include <rex/string/text.h>

namespace x360n {
// Called after all user settings settle, before drivers or original images load.
inline rex::Result<void> PrepareDashboardPaths(rex::kernel::TitlePaths& paths, std::string_view version,
                                  std::span<const DashboardAsset> assets) {
  auto error = paths.update_root.Get().empty() ? VerifyDashboardAssets(paths.game_root.Get(), version, assets)
      : "Dashboard Retail " + std::string(version) + ": update_data_root overlays are not supported; use the complete matching original package";
  if (!error.empty()) {
    REXLOG_ERROR("{}", error);
    return rex::Err(rex::ErrorCategory::kValidation, error);
  }
  auto& host = REX_PATHS();
  const auto cache = host.cache_root.Get();
  REX_TRY_VOID(host.cache_root.SetText(rex::SettingLayer::kRuntime,
      rex::string::PathToUTF8(DashboardCacheRoot(cache.empty() ? host.user_root.Get() / "cache" : cache, version))));
  // An explicit root also disables the SDK's parent-directory metadata search.
  const auto metadata = paths.metadata_root.Get();
  const auto isolated_metadata = metadata.empty() || paths.metadata_root.GetLayer() == rex::SettingLayer::kDefault
      ? paths.game_root.Get() / "metadata" : DashboardCacheRoot(metadata, version);
  REX_TRY_VOID(paths.metadata_root.SetText(rex::SettingLayer::kRuntime,
      rex::string::PathToUTF8(isolated_metadata)));
  REXLOG_INFO("Dashboard Retail {}: verified {} original dependencies in {}; no cross-version fallback",
              version, assets.size(), rex::string::PathToUTF8(paths.game_root.Get()));
  return rex::Ok();
}
}
