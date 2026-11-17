#include "system_ui.h"
#include "original_assets.h"
#include "experimental_region.h"
#include "system_ext_partition.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/object_table.h>
#include <rex/logging/log_category.h>
#include <rex/filesystem/file_system.h>
#include <rex/string/text.h>
#include <cstring>

#ifdef _WIN32
#define SESSION_UI_EXPORT extern "C" __declspec(dllexport)
#else
#define SESSION_UI_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// The supervisor selects this version-specific host from its verified dashboard
// receipt. Original assets remain in that dashboard's own directory.
SESSION_UI_EXPORT bool X360nSystemUiConfigure(rex::SettingsRegistry* settings) {
  if (!settings) return false;
  const auto registered = dash17559::RegisterSystemUiSettings(*settings);
  if (!registered) REXLOG_ERROR("17559 system UI settings: {}", registered.error().message);
  return bool(registered);
}
SESSION_UI_EXPORT void X360nSystemUiUnconfigure(rex::SettingsRegistry* settings) {
  if (settings) settings->Unregister(dash17559::GetSystemUiSettings());
}
SESSION_UI_EXPORT bool X360nSystemUiInstall(rex::kernel::TitleRuntime* runtime, const char* root_utf8,
                                           const char* native_utf8) {
  if (!runtime || !root_utf8 || !native_utf8 || !runtime->IsLoaded()) return false;
  const auto root = rex::string::to_path(root_utf8);
  const auto error = x360n::VerifyDashboardAssets(root, "17559", dash17559::kOriginalAssets);
  if (!error.empty()) { REXLOG_ERROR("{}", error); return false; }
  // Original HUD skins name their font files through media:. Never replace
  // a title-owned mapping with system assets or search another dashboard.
  auto& files = runtime->GetFileSystem();
  const auto media_target = files.FindLink("media:");
  if (media_target && *media_target != "\\SystemRoot") {
    REXLOG_ERROR("17559 system UI: media: is already owned by {}", *media_target);
    return false;
  }
  const auto executable = runtime->GetModuleRegistry().GetExecutable();
  if (!executable) return false;
  auto module = runtime->GetObjectTable().Get<rex::kernel::ModuleObject>(*executable);
  if (!module || !module->processor_module) return false;
  dash17559::ExperimentalRegion region;
  if (region.Map(*module->processor_module->GetAddressSpace(), true) != dash17559::ExperimentalRegion::Result::Mapped) {
    REXLOG_ERROR("17559 system UI: experimental system region unavailable");
    return false;
  }
  module = {};
  REXLOG_WARN("[compatibility-experiment] 17559 system UI: 0x8E030000 size=65536 RW; original contents unknown");
  if (!files.Mount({"\\SystemRoot", rex::filesystem::ProviderType::kHostDirectory, root,
                    rex::filesystem::MountFlags::kNone, {}})) return false;
  if (!files.Link("sys17559:", "\\SystemRoot")) return false;
  if (!media_target && !files.Link("media:", "\\SystemRoot")) return false;
  if (!dash17559::MountSystemExtPartition(files, root)) return false;
  dash17559::InstallSystemUi(*runtime, "sys17559:", native_utf8);
  REXLOG_INFO("17559 system UI: session host installed with verified original assets at {}", root_utf8);
  return true;
}
SESSION_UI_EXPORT void X360nSystemUiRelease(rex::kernel::TitleRuntime* runtime) {
  if (runtime) dash17559::ReleaseSystemUiHost(*runtime);
}
// The one original XAM 17559 image of the title process, owned by this
// dashboard profile and shared with the title's avatar service.
SESSION_UI_EXPORT const char* X360nSystemUiXamModule() { return dash17559::SystemXamPath().c_str(); }
