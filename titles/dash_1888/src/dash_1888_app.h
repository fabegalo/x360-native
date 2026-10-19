#pragma once
#include <memory>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/cvar.h>
#include "boot_intro.h"
#include "../../common/dashboard_paths.h"
#include "../../common/launch_handoff.h"
#include "../../common/guest_capture.h"
#include "original_assets.h"
#include "system_ui.h"

REXCVAR_DECLARE(bool, dash1888_boot_intro);
REXCVAR_DEFINE_STRING(x360n_guest_capture, "", "Diagnostics", "Capture actual GPU output every two seconds (opt-in)");
REXCVAR_DEFINE_UINT32(x360n_guest_capture_seconds, 30, "Diagnostics", "GPU capture duration, limited to 300 seconds");

class Dash1888App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Dash1888App>(new Dash1888App(ctx, "dash_1888", PPCImageConfig));
  }
  bool ConstructRuntime(const rex::PathConfig& defaults) override {
    auto paths = defaults;
    return x360n::PrepareDashboardPaths(paths, "1888", dash1888::kOriginalAssets) &&
           rex::ReXApp::ConstructRuntime(paths);
  }
  void OnLoadXexImage(std::string& path) override { path = "game:\\dash.xex"; }
  void OnPreLaunchModule() override {
    guest_capture_.Start(*runtime(), REXCVAR_GET(x360n_guest_capture), REXCVAR_GET(x360n_guest_capture_seconds));
    x360n::ImportLaunchData(*runtime());
    if (REXCVAR_GET(dash1888_boot_intro) && !x360n::IsDashboardReturn())
      dash1888::InstallBootIntro(*runtime());
    dash1888::InstallSystemUi(*runtime());
  }
  void OnGuestThreadExit(rex::system::XThread*) override {
    dash1888::ReleaseSystemUi();
    x360n::ExportLaunchRequest(*runtime());
  }
  bool OnWindowCloseRequested() override { x360n::ExportWindowClose(); return true; }
  void OnShutdown() override { guest_capture_.Stop(); }
  void OnPostSetup() override {
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(
        "\\SystemRoot", game_data_root(), true);
    if (!device->Initialize() || !runtime()->file_system()->RegisterDevice(std::move(device))) {
      REX_FATAL("dash_1888: could not mount its verified original package at \\SystemRoot");
    }
  }
 private:
  x360n::GuestCapture guest_capture_;
};
