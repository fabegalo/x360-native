#include "system_ui.h"
#include "original_assets.h"
#include "experimental_region.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/ppc/func.h>
#include <cstring>

#ifdef _WIN32
#define SESSION_UI_EXPORT extern "C" __declspec(dllexport)
#else
#define SESSION_UI_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// The supervisor selects this version-specific host from its verified dashboard
// receipt. Original assets remain in that dashboard's own directory.
SESSION_UI_EXPORT bool X360nSystemUiInstall(rex::Runtime* runtime, const char* root_utf8,
                                           const char* native_utf8) {
  const auto root = std::filesystem::path(std::u8string(root_utf8, root_utf8 + std::strlen(root_utf8)));
  const auto error = x360n::VerifyDashboardAssets(root, "17559", dash17559::kOriginalAssets);
  if (!error.empty()) { REXLOG_ERROR("{}", error); return false; }
  // Original HUD skins name their font files through media:. Never replace
  // a title-owned mapping with system assets or search another dashboard.
  std::string media_target;
  if (runtime->file_system()->FindSymbolicLink("media:", media_target) && media_target != "\\SystemRoot") {
    REXLOG_ERROR("17559 system UI: media: is already owned by {}", media_target);
    return false;
  }
  dash17559::ExperimentalRegion region;
  if (region.Map(*runtime->memory(), true) != dash17559::ExperimentalRegion::Result::Mapped) {
    REXLOG_ERROR("17559 system UI: experimental system region unavailable");
    return false;
  }
  REXLOG_WARN("[compatibility-experiment] 17559 system UI: 0x8E030000 size=65536 RW; original contents unknown");
  auto device = std::make_unique<rex::filesystem::HostPathDevice>("\\SystemRoot", root, true);
  if (!device->Initialize() || !runtime->file_system()->RegisterDevice(std::move(device))) return false;
  runtime->file_system()->RegisterSymbolicLink("sys17559:", "\\SystemRoot");
  runtime->file_system()->RegisterSymbolicLink("media:", "\\SystemRoot");
  dash17559::InstallSystemUi(*runtime, "sys17559:", native_utf8);
  REXLOG_INFO("17559 system UI: session host installed with verified original assets at {}", root_utf8);
  return true;
}
SESSION_UI_EXPORT void X360nSystemUiRelease() { dash17559::ReleaseSystemUiHost(); }
