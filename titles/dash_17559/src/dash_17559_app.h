#pragma once
#include <rex/filesystem/file_system.h>
#include <rex/kernel/title_paths.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/object_table.h>
#include <rex/platform/process.h>
#include <rex/logging/logger.h>
#include <utility>
#include "../../common/session_title_app.h"
#include "../../common/dashboard_paths.h"
#include "experimental_region.h"
#include "boot_intro.h"
#include "launch_context.h"
#include "display_transition.h"
#include "system_ui.h"
#include "system_ext_partition.h"
#include "original_assets.h"

class Dash17559App : public x360n::SessionTitleApp {
 public:
  Dash17559App() : SessionTitleApp("dash_17559", kPPCImageConfig) {}
  ~Dash17559App() override { Shutdown(); }

 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(SessionTitleApp::OnConfigure(settings));
    REX_TRY_VOID(dash17559::RegisterSystemUiSettings(settings));
    settings_ = rex::ScopeGuard([&settings] { settings.Unregister(dash17559::GetSystemUiSettings()); });
    return GetTitleRuntime().GetTitlePaths().entry.SetText(rex::SettingLayer::kTitle, "dash.xex");
  }
  rex::Result<void> OnSettingsLoaded(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(SessionTitleApp::OnSettingsLoaded(settings));
    return x360n::PrepareDashboardPaths(GetTitleRuntime().GetTitlePaths(), "17559", dash17559::kOriginalAssets);
  }
  rex::Result<void> OnSessionLaunch(rex::kernel::TitleRuntime& title) override {
    const auto root = title.GetTitlePaths().game_root.Get();
    REX_TRY_VOID(title.GetFileSystem().Mount({"\\SystemRoot", rex::filesystem::ProviderType::kHostDirectory,
        root, rex::filesystem::MountFlags::kNone, {}}));
    if (!dash17559::MountSystemExtPartition(title.GetFileSystem(), root))
      return rex::Err(rex::ErrorCategory::kIO, "17559: system extension partition mount failed");
    const auto executable = title.GetModuleRegistry().GetExecutable();
    if (!executable) return rex::Err(rex::ErrorCategory::kModule, "17559: no executable module");
    auto module = title.GetObjectTable().Get<rex::kernel::ModuleObject>(*executable);
    if (!module || !module->processor_module)
      return rex::Err(rex::ErrorCategory::kModule, "17559: executable image is unavailable");
    const auto mapped = experimental_region_.Map(*module->processor_module->GetAddressSpace(),
        dash17559::GetSystemUiSettings().experimental_region.Get());
    using Region = dash17559::ExperimentalRegion;
    if (mapped == Region::Result::Overlap || mapped == Region::Result::AllocationFailed)
      return rex::Err(rex::ErrorCategory::kMemory, "17559: experimental region setup failed");
    module = {};
    REXLOG_WARN("[compatibility-experiment] dash17559_experimental_region={}; "
                "0x8E030000 size=65536 RW; actual original contents unknown",
                mapped == Region::Result::Disabled ? "disabled" : "enabled");
    if (rex::platform::IsDebuggerAttached() && x360n::GetLaunchSettings().guest_fault_context.Get().empty()) {
      const auto configured = rex::string::to_path(REX_LOGGER().file_name.Get());
      auto diagnostic = configured.is_absolute() ? configured : log_root.Get() / "dash_17559.log";
      diagnostic += ".ppc.txt";
      REX_TRY_VOID(x360n::GetLaunchSettings().guest_fault_context.SetText(rex::SettingLayer::kRuntime,
          rex::string::PathToUTF8(diagnostic)));
    }
    dash17559::InstallDisplayTransition(title);
    ui_installed_ = true;
    dash17559::InstallSystemUi(title);
    return rex::Ok();
  }
  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime& title) override {
    REX_TRY_VOID(SessionTitleApp::OnTitleLaunch(title));
    if (x360n::IsDashboardReturn() && !dash17559::PrepareReturnContext(title))
      return rex::Err(rex::ErrorCategory::kValidation, "17559: unsupported dashboard return flags");
    if (dash17559::GetSystemUiSettings().boot_intro.Get() && !x360n::IsDashboardReturn())
      dash17559::InstallBootIntro(title);
    return rex::Ok();
  }
  std::string GetSystemXamPath() const override { return dash17559::SystemXamPath(); }
  void OnSessionUnload(rex::kernel::TitleRuntime& title) override {
    if (std::exchange(ui_installed_, false)) dash17559::ReleaseSystemUiHost(title);
    experimental_region_ = {};
    SessionTitleApp::OnSessionUnload(title);
  }
  void OnRelease() override {
    SessionTitleApp::OnRelease();
    settings_.Reset();
  }

 private:
  rex::ScopeGuard settings_;
  dash17559::ExperimentalRegion experimental_region_;
  bool ui_installed_ = false;
};
