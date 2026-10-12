// dash_17559 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <memory>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

class Dash17559App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Dash17559App>(new Dash17559App(ctx, "dash_17559",
        PPCImageConfig));
  }

  // x360-native: ReXApp loads game:\default.xex by default (rex_app.cpp:262);
  // the dashboard's executable is dash.xex, the same file the manifest's
  // [entrypoint] was generated from.
  void OnLoadXexImage(std::string& xex_image) override { xex_image = "game:\\dash.xex"; }

  // x360-native: dash.xex links media: to \SystemRoot itself (ObCreateSymbolicLink,
  // "Registered symbolic link: media: => \SystemRoot") and opens media:\shrdres.xzp
  // (run 20261012T011724Z-dash17559-run-8: "device not found"). On the console that is
  // the system partition with dash.xex and shrdres.xzp; here it is the package folder
  // (--game_data_root), the same files, mounted read-only.
  void OnPostSetup() override {
    auto device = std::make_unique<rex::filesystem::HostPathDevice>("\\SystemRoot",
                                                                    game_data_root(), true);
    if (!device->Initialize() || !runtime()->file_system()->RegisterDevice(std::move(device))) {
      REXLOG_ERROR("dash_17559: could not mount {} at \\SystemRoot", game_data_root().string());
    }
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
