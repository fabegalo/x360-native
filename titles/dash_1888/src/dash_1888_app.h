#pragma once
#include <rex/filesystem/file_system.h>
#include "boot_intro.h"
#include "../../common/dashboard_paths.h"
#include "../../common/session_title_app.h"
#include "original_assets.h"
#include "system_ui.h"
#include "guide_diagnostic.h"
#include <rex/hid/input_system.h>

class Dash1888App : public x360n::SessionTitleApp {
 public:
  Dash1888App() : SessionTitleApp("dash_1888", kPPCImageConfig) {}
  ~Dash1888App() override { Shutdown(); }
 protected:
  void OnRegisterModules(std::vector<rex::IModule*>& modules) override {
    SessionTitleApp::OnRegisterModules(modules);
    modules.push_back(&guide_diagnostic_);
  }
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(SessionTitleApp::OnConfigure(settings));
    REX_TRY_VOID(settings.Register(dash1888::GetSystemUiSettings()));
    ui_settings_ = rex::ScopeGuard([&settings] { settings.Unregister(dash1888::GetSystemUiSettings()); });
    return GetTitleRuntime().GetTitlePaths().entry.SetText(rex::SettingLayer::kDefault, "dash.xex");
  }
  rex::Result<void> OnSettingsLoaded(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(SessionTitleApp::OnSettingsLoaded(settings));
    REX_TRY_VOID(x360n::PrepareDashboardPaths(GetTitleRuntime().GetTitlePaths(), "1888", dash1888::kOriginalAssets));
    const auto probe = dash1888::GetSystemUiSettings().input_probe.Get();
    if (probe == 1 || probe == 2) {
      auto& drivers = GetInput().GetSettings().drivers;
      REX_TRY_VOID(drivers.SetText(rex::SettingLayer::kRuntime, "dash1888-probe," + drivers.Get()));
    }
    return rex::Ok();
  }
  rex::Result<void> OnSessionLaunch(rex::kernel::TitleRuntime& runtime) override {
    rex::filesystem::MountEntry mount;
    mount.path = "\\SystemRoot";
    mount.source = runtime.GetTitlePaths().game_root.Get();
    REX_TRY_VOID(runtime.GetFileSystem().Mount(mount));
    ui_installed_ = true;
    dash1888::InstallSystemUi(runtime);
    return rex::Ok();
  }
  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime& runtime) override {
    REX_TRY_VOID(SessionTitleApp::OnTitleLaunch(runtime));
    if (dash1888::GetSystemUiSettings().boot_intro.Get() && !x360n::IsDashboardReturn())
      dash1888::InstallBootIntro(runtime);
    return rex::Ok();
  }
  void OnSessionUnload(rex::kernel::TitleRuntime& runtime) override {
    if (std::exchange(ui_installed_, false)) dash1888::ReleaseSystemUi(runtime);
    SessionTitleApp::OnSessionUnload(runtime);
  }
  void OnRelease() override {
    SessionTitleApp::OnRelease();
    ui_settings_.Reset();
  }
 private:
  dash1888::GuideDiagnosticProvider guide_diagnostic_;
  rex::ScopeGuard ui_settings_;
  bool ui_installed_ = false;
};
