#pragma once
#include <string>
#include <rex/settings_registry.h>
#include <rex/setting.h>
struct PPCContext;
namespace rex::kernel { class TitleRuntime; }
namespace dash17559 {
class SystemUiSettings final : public rex::SettingOwner {
 public:
  SystemUiSettings() : SettingOwner("dash17559") {}
  REX_SETTING(bool, experimental_region, true, "Experimental zero-filled system region; actual contents unknown");
  REX_SETTING(bool, boot_intro, false, "Run the original Retail 17559 boot module before the dashboard");
  REX_SETTING(bool, signin_ui, false, "Submit one original Sign-in request for manual validation");
  REX_SETTING(bool, storage_io_diagnostics, false, "Log original storage selector output");
  REX_SETTING(rex::u32, guide_probe_seconds, 0, "Request original Guide after 1..120 seconds; zero disables");
  REX_SETTING(rex::u32, selector_probe_seconds, 0, "Request original storage selector after 1..120 seconds");
  REX_SETTING(rex::u32, message_box_probe_seconds, 0, "Request original message box after 1..120 seconds");
  REX_SETTING(rex::u32, keyboard_probe_seconds, 0, "Request original keyboard after 1..120 seconds");
  REX_SETTING(bool, keyboard_probe_branded, false, "Select the original branded keyboard for the diagnostic");
  REX_SETTING(rex::u32, ui_self, 0, "Verified original XAM module HMODULE", rex::kNoSave);
};
inline SystemUiSettings& GetSystemUiSettings() { static SystemUiSettings settings; return settings; }
inline rex::Result<void> RegisterSystemUiSettings(rex::SettingsRegistry& registry) {
  REX_TRY_VOID(registry.Register(GetSystemUiSettings()));
  for (const auto* name : {"experimental_region", "boot_intro", "signin_ui", "storage_io_diagnostics",
                          "guide_probe_seconds", "selector_probe_seconds", "message_box_probe_seconds",
                          "keyboard_probe_seconds", "keyboard_probe_branded", "ui_self"})
    registry.AddAlias(std::string("dash17559_") + name, std::string("dash17559.") + name);
  return rex::Ok();
}
void InstallSystemUi(rex::kernel::TitleRuntime& runtime, std::string drive = "game:", std::string native_directory = "");
void DrawSystemUiFrame(PPCContext& ctx);
void ReleaseSystemUiHost(rex::kernel::TitleRuntime& runtime);
// Guest path of the single original XAM image this host binds in a title
// process (its own dash_17559_xam_ui AOT); empty when nothing is bound.
const std::string& SystemXamPath();
}
