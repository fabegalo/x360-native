// dash_6717 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <memory>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include "../../common/dashboard_paths.h"
#include "original_assets.h"

class Dash6717App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  bool ConstructRuntime(const rex::PathConfig& defaults) override {
    auto paths = defaults;
    return x360n::PrepareDashboardPaths(paths, "6717", dash6717::kOriginalAssets) &&
           rex::ReXApp::ConstructRuntime(paths);
  }

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Dash6717App>(new Dash6717App(ctx, "dash_6717",
        PPCImageConfig));
  }

  // Own original executable and assets. No 17559 modules or compatibility region.
  void OnLoadXexImage(std::string& path) override { path = "game:\\dash.xex"; }

  // x360-native: this dash.xex links media: and dashmedia: to \SystemRoot itself
  // ("Registered symbolic link: media: => \SystemRoot", run
  // 20261012T054514Z-dash6717-media-off-debug) and opens media:\shrdres.xzp and
  // media:\XenonJKLatin.xtt. Without a \SystemRoot device every open failed with
  // "device not found" (run 20261012T054026Z-dash6717-cam). As in 17559, \SystemRoot
  // is this profile's package folder (--game_data_root), read-only; nothing from 17559.
  void OnPostSetup() override {
    auto device = std::make_unique<rex::filesystem::HostPathDevice>("\\SystemRoot",
                                                                    game_data_root(), true);
    if (!device->Initialize() || !runtime()->file_system()->RegisterDevice(std::move(device))) {
      REXLOG_ERROR("dash_6717: could not mount {} at \\SystemRoot", game_data_root().string());
    }
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnPostLoadXexImage() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
