#include "launch_handoff.h"
#include "launch_request.h"
#include "games_played.h"
#include <mutex>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/logging/assert.h>
#include <rex/kernel/xam/module.h>
#include <rex/system/user_module.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/platform/dynlib.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_STRING(x360n_handoff_out, "", "Supervisor", "Per-run host handoff output");
REXCVAR_DEFINE_STRING(x360n_handoff_in, "", "Supervisor", "Previous title's launch data");
REXCVAR_DEFINE_STRING(x360n_profile_root, "", "Supervisor", "Profile store owned by this dashboard session");
REXCVAR_DEFINE_STRING(x360n_dashboard_version, "", "Supervisor", "Version of the session dashboard");
REXCVAR_DEFINE_STRING(x360n_system_ui_host, "", "Supervisor", "Verified dashboard session UI host");
REXCVAR_DEFINE_STRING(x360n_system_ui_root, "", "Supervisor", "Original assets of the session dashboard");
REXCVAR_DECLARE(uint32_t, license_mask);

namespace x360n {
namespace {
std::mutex handoff_mutex;
bool dashboard_return = false;
rex::platform::DynamicLibrary system_ui_host;
void (*system_ui_release)() = nullptr;
std::string system_xam_path;
std::filesystem::path Utf8Path(const std::string& value) {
  return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
void Publish(const std::string& output, const LaunchRequest& request) {
  // Guest exit and the user's close request can arrive on different threads.
  // Preserve the first completed event, without racing on the temporary file.
  std::lock_guard lock(handoff_mutex);
  if (std::filesystem::exists(Utf8Path(output))) return;
  if (!WriteLaunchRequest(Utf8Path(output), request))
    REXLOG_ERROR("[supervisor] could not write handoff {}", output);
  else REXLOG_INFO("[supervisor] handoff kind={}, path={}", uint32_t(request.kind), request.path);
}
}
bool IsDashboardReturn() { return dashboard_return; }
const std::string& SystemXamModulePath() { return system_xam_path; }
void ImportLaunchData(rex::Runtime& runtime) {
  dashboard_return = false;
  const auto ui_host = REXCVAR_GET(x360n_system_ui_host);
  if (!ui_host.empty()) {
    const auto root = REXCVAR_GET(x360n_system_ui_root);
    const auto path = Utf8Path(ui_host);
    if (root.empty() || !path.is_absolute() || !system_ui_host.Load(path))
      REX_FATAL("Missing verified dashboard system UI host: {}", ui_host);
    using Install = bool (*)(rex::Runtime*, const char*, const char*);
    auto install = system_ui_host.GetSymbol<Install>("X360nSystemUiInstall");
    // The host composes at VdSwap; titles need no render hook.
    system_ui_release = system_ui_host.GetSymbol<decltype(system_ui_release)>("X360nSystemUiRelease");
    const auto directory = path.parent_path().u8string();
    if (!install || !system_ui_release ||
        !install(&runtime, root.c_str(), reinterpret_cast<const char*>(directory.c_str())))
      REX_FATAL("Invalid dashboard system UI host or original assets: {}", ui_host);
    // Optional export: hosts that bind an original XAM image share it.
    if (auto xam = system_ui_host.GetSymbol<const char* (*)()>("X360nSystemUiXamModule")) system_xam_path = xam();
  }
  auto input = REXCVAR_GET(x360n_handoff_in);
  LaunchRequest request;
  if (!input.empty() &&
      (!ReadLaunchRequest(Utf8Path(input), request) || request.kind == LaunchRequest::WindowClose))
    REX_FATAL("Invalid supervisor launch-data record: {}", input);
  const auto profile_root = REXCVAR_GET(x360n_profile_root);
  if (!profile_root.empty()) {
    auto* user = runtime.kernel_state()->user_profile();
    if (!user->InitializeLocal(Utf8Path(profile_root)))
      REX_FATAL("Invalid dashboard session profile store: {}", profile_root);
    const auto identity = user->identity();
    REXLOG_INFO("[supervisor] session profile xuid={:016X} state={} root={}",
        identity.xuid, identity.signin_state, profile_root);
    // A game launched with a signed-in account joins that account's played
    // titles; the dashboard and other system titles are not recorded.
    if (runtime.kernel_state()->GetExecutableModule()) {
      const auto title_id = runtime.kernel_state()->title_id();
      const auto version_text = REXCVAR_GET(x360n_dashboard_version);
      const auto version = ParseDashboardVersion(version_text);
      if (!version_text.empty() && !version)
        REX_FATAL("Invalid session dashboard version: {}", version_text);
      // The package the dashboard launched, when this title came from one.
      std::optional<LaunchedContent> content;
      if (!input.empty() && request.kind == LaunchRequest::Image && !request.path.empty())
        content = ReadLaunchedContent(Utf8Path(request.path), title_id);
      // Use the same active unlock mask exposed to the game by XAM. Reading it
      // does not grant a license or change the installed package.
      switch (DecideGamesPlayed(version, content, REXCVAR_GET(license_mask))) {
        case GamesPlayedDecision::Record:
          user->RecordTitlePlayed(title_id);
          break;
        case GamesPlayedDecision::SkipArcadeTrial:
          REXLOG_INFO("[supervisor] title {:08X} is an Arcade trial; dashboard {} does not "
                      "add it to Games Played", title_id, version_text);
          break;
        case GamesPlayedDecision::UnknownDashboard:
          REXLOG_WARN("[supervisor] title {:08X} is an Arcade trial and the session dashboard "
                      "version is unknown; Games Played not updated", title_id);
          break;
      }
    }
  }
  if (input.empty()) return;
  dashboard_return = request.kind != LaunchRequest::Image;
  auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  auto& data = xam->loader_data();
  data.launch_data_present = request.data_present;
  data.launch_data = std::move(request.data);
  data.launch_flags = request.flags;
  REXLOG_INFO("[supervisor] imported {} bytes of launch data; kind={} flags={:08X} dashboard_return={}",
      data.launch_data.size(), uint32_t(request.kind), request.flags, dashboard_return);
}
void ExportLaunchRequest(rex::Runtime& runtime) {
  if (system_ui_release) { system_ui_release(); system_ui_release = nullptr; }
  const auto output = REXCVAR_GET(x360n_handoff_out);
  if (output.empty()) return;
  auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  const auto& data = xam->loader_data();
  LaunchRequest request;
  using Exit = rex::kernel::xam::XamModule::LoaderData::ExitRequest;
  request.flags = data.launch_flags;
  request.data_present = data.launch_data_present;
  request.data = data.launch_data;
  if (data.exit_request == Exit::Firmware) {
    request.kind = LaunchRequest::FirmwareExit;
    request.flags = data.firmware_routine;
    request.data_present = false;
    request.data.clear();
  } else if (data.exit_request == Exit::Dashboard) {
    request.kind = LaunchRequest::Dashboard;
  } else if (data.exit_request == Exit::Terminate) {
    request.kind = LaunchRequest::TitleTerminate;
  } else if (data.launch_path.empty()) {
    request.kind = LaunchRequest::GuestExit;
  } else {
    auto* entry = runtime.file_system()->ResolvePath(data.launch_path);
    auto* host = dynamic_cast<rex::filesystem::HostPathEntry*>(entry);
    if (!host) {
      REXLOG_ERROR("[supervisor] launch path has no host file: {}", data.launch_path);
      return;
    }
    auto path = host->host_path().u8string();
    request.path.assign(path.begin(), path.end());
  }
  Publish(output, request);
}
void ExportWindowClose() {
  const auto output = REXCVAR_GET(x360n_handoff_out);
  if (output.empty()) return;
  LaunchRequest request; request.kind = LaunchRequest::WindowClose;
  Publish(output, request);
}
}  // namespace x360n
