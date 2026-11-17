#pragma once
#include <string>
#include <rex/setting.h>
#include <rex/settings_registry.h>
struct PPCContext;
namespace rex::kernel { class TitleRuntime; }
namespace x360n {
class LaunchSettings final : public rex::SettingOwner {
 public:
  LaunchSettings() : SettingOwner("x360n") {}
  REX_SETTING(std::string, handoff_out, "", "Per-run host handoff output");
  REX_SETTING(std::string, handoff_in, "", "Previous title's launch data");
  REX_SETTING(std::string, profile_root, "", "Profile store owned by this dashboard session");
  REX_SETTING(std::string, dashboard_version, "", "Version of the session dashboard");
  REX_SETTING(bool, avatar_service, true, "Use the original 17559 avatar catalog and AOT algorithms");
  REX_SETTING(std::string, avatar_xam_17559, "", "Original XAM of the session dashboard, verified by the supervisor");
  REX_SETTING(std::string, avatar_catalog_17559, "", "Original avatar catalog of the session dashboard, verified by the supervisor");
  REX_SETTING(std::string, system_ui_host, "", "Verified dashboard session UI host");
  REX_SETTING(std::string, system_ui_root, "", "Original assets of the session dashboard");
  REX_SETTING(std::string, guest_fault_context, "", "Optional private bounded guest fault diagnostic");
  REX_SETTING(std::string, guest_capture, "", "Optional directory for actual GPU output captures");
  REX_SETTING(rex::u32, guest_capture_seconds, 30, "GPU capture duration, limited to 300 seconds");
};
LaunchSettings& GetLaunchSettings();
rex::Result<void> RegisterLaunchSettings(rex::SettingsRegistry& settings);
void ImportLaunchData(rex::kernel::TitleRuntime& runtime, std::string bound_xam_path = {});
bool IsDashboardReturn();
// Guest path of the session dashboard's original XAM image, bound by its system
// UI host (one image per process, shared by the Guide and the avatar service).
// The dashboard supplies its own binding to ImportLaunchData.
const std::string& SystemXamModulePath();
void ExportLaunchRequest(rex::kernel::TitleRuntime& runtime);
void ExportWindowClose();
// Called after guest threads stop, while the single title runtime is still loaded.
void ReleaseSessionUi(rex::kernel::TitleRuntime& runtime);
rex::Result<void> ConfigureSessionUi(rex::SettingsRegistry& settings);
void UnconfigureSessionUi(rex::SettingsRegistry& settings);
}
