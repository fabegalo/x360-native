#pragma once
#include <memory>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include "../../common/dashboard_paths.h"
#include "../../common/launch_handoff.h"
#include "original_assets.h"

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
  void OnPreLaunchModule() override { x360n::ImportLaunchData(*runtime()); }
  void OnGuestThreadExit(rex::system::XThread*) override {
    x360n::ExportLaunchRequest(*runtime());
  }
  bool OnWindowCloseRequested() override { x360n::ExportWindowClose(); return true; }
  void OnPostSetup() override {
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(
        "\\SystemRoot", game_data_root(), true);
    if (!device->Initialize() || !runtime()->file_system()->RegisterDevice(std::move(device))) {
      REX_FATAL("dash_1888: could not mount its verified original package at \\SystemRoot");
    }
  }
};
