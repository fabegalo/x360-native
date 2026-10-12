// motocross_madness - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include "../../common/launch_handoff.h"

class MotocrossMadnessApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  void OnPreLaunchModule() override { x360n::ImportLaunchData(*runtime()); }
  void OnGuestThreadExit(rex::system::XThread*) override {
    x360n::ExportLaunchRequest(*runtime());
  }
  bool OnWindowCloseRequested() override { x360n::ExportWindowClose(); return true; }

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<MotocrossMadnessApp>(new MotocrossMadnessApp(ctx, "motocross_madness",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
