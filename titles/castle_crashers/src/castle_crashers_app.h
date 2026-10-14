// castle_crashers - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include "../../common/launch_handoff.h"
#include "../../common/guest_capture.h"

REXCVAR_DEFINE_STRING(x360n_guest_capture, "", "Diagnostics", "Capture actual GPU output every two seconds (thirty seconds by default)");
REXCVAR_DEFINE_UINT32(x360n_guest_capture_seconds, 30, "Diagnostics", "GPU capture duration, limited to 300 seconds");

class CastleCrashersApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  void OnPreLaunchModule() override {
    x360n::ImportLaunchData(*runtime());
    guest_capture_.Start(*runtime(), REXCVAR_GET(x360n_guest_capture), REXCVAR_GET(x360n_guest_capture_seconds));
  }
  void OnShutdown() override { guest_capture_.Stop(); }
  void OnGuestThreadExit(rex::system::XThread*) override {
    x360n::ExportLaunchRequest(*runtime());
  }
  bool OnWindowCloseRequested() override { x360n::ExportWindowClose(); return true; }

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<CastleCrashersApp>(new CastleCrashersApp(ctx, "castle_crashers",
        PPCImageConfig));
  }

 private:
  x360n::GuestCapture guest_capture_;

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
