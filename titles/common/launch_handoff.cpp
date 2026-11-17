#include "launch_handoff.h"
#include "launch_request.h"
#include "games_played.h"
#include <mutex>
#include <utility>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/kernel_settings.h>
#include <rex/kernel/module.h>
#include <rex/logging/log_category.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/loader_state.h>
#include <rex/xam/user_profile.h>
#include <rex/filesystem/file_system.h>
#include <rex/platform/file.h>
#include <rex/platform/dynlib.h>

namespace x360n {
LaunchSettings& GetLaunchSettings() { static LaunchSettings settings; return settings; }
rex::Result<void> RegisterLaunchSettings(rex::SettingsRegistry& registry) {
  REX_TRY_VOID(registry.Register(GetLaunchSettings()));
  registry.AddAlias("guide_chord_ms", "hid.guide_chord_ms");
  registry.AddAlias("guide_chord_users", "hid.guide_chord_users");
  // Legacy non-dotted options must be known before CLI parsing, even when their
  // original-system owner comes from the session UI library loaded afterwards.
  for (const auto* name : {"experimental_region", "boot_intro", "signin_ui", "storage_io_diagnostics",
                          "guide_probe_seconds", "selector_probe_seconds", "message_box_probe_seconds",
                          "keyboard_probe_seconds", "keyboard_probe_branded", "ui_self"})
    registry.AddAlias(std::string("dash17559_") + name, std::string("dash17559.") + name);
  for (const auto* name : {"boot_intro", "guide_probe_seconds", "guide_probe_cycles",
                          "ui_lifecycle_trace", "input_probe", "ui_input_trace"})
    registry.AddAlias(std::string("dash1888_") + name, std::string("dash1888.") + name);
  for (const auto* name : {"handoff_out", "handoff_in", "profile_root", "dashboard_version",
                           "avatar_service", "avatar_xam_17559", "avatar_catalog_17559",
                           "system_ui_host", "system_ui_root", "guest_fault_context", "guest_capture", "guest_capture_seconds"})
    registry.AddAlias(std::string("x360n_") + name, std::string("x360n.") + name);
  return rex::Ok();
}
namespace {
std::mutex handoff_mutex;
bool dashboard_return = false;
rex::platform::DynamicLibrary system_ui_host;
void (*system_ui_release)(rex::kernel::TitleRuntime*) = nullptr;
void (*system_ui_unconfigure)(rex::SettingsRegistry*) = nullptr;
std::filesystem::path configured_ui_host;
std::string system_xam_path;
std::filesystem::path Utf8Path(const std::string& value) {
  return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
// Supervisor records carry ordinary paths; host I/O goes through the long form.
std::filesystem::path HostFile(const std::string& value) {
  return rex::platform::HostPath(Utf8Path(value));
}
void Publish(const std::string& output, const LaunchRequest& request) {
  // Guest exit and the user's close request can arrive on different threads.
  // Preserve the first completed event, without racing on the temporary file.
  std::lock_guard lock(handoff_mutex);
  if (std::filesystem::exists(HostFile(output))) return;
  if (!WriteLaunchRequest(HostFile(output), request))
    REXLOG_ERROR("[supervisor] could not write handoff {}", output);
  else REXLOG_INFO("[supervisor] handoff kind={}, path={}", uint32_t(request.kind), request.path);
}
}
bool IsDashboardReturn() { return dashboard_return; }
const std::string& SystemXamModulePath() { return system_xam_path; }
rex::Result<void> ConfigureSessionUi(rex::SettingsRegistry& settings) {
  const auto host = GetLaunchSettings().system_ui_host.Get();
  if (host.empty()) return rex::Ok();
  const auto path = Utf8Path(host);
  if (GetLaunchSettings().system_ui_root.Get().empty() || !path.is_absolute() || !system_ui_host.Load(path))
    return rex::Err(rex::ErrorCategory::kModule, "Missing verified dashboard system UI host: " + host);
  using Configure = bool (*)(rex::SettingsRegistry*);
  const auto configure = system_ui_host.GetSymbol<Configure>("X360nSystemUiConfigure");
  const auto unconfigure = system_ui_host.GetSymbol<decltype(system_ui_unconfigure)>("X360nSystemUiUnconfigure");
  if (!configure || !unconfigure || !configure(&settings))
    return rex::Err(rex::ErrorCategory::kModule, "Dashboard system UI host has incompatible settings ABI: " + host);
  configured_ui_host = path;
  system_ui_unconfigure = unconfigure;
  return rex::Ok();
}
void UnconfigureSessionUi(rex::SettingsRegistry& settings) {
  if (const auto release = std::exchange(system_ui_unconfigure, nullptr)) release(&settings);
  configured_ui_host.clear();
}
void ImportLaunchData(rex::kernel::TitleRuntime& runtime, std::string bound_xam_path) {
  system_xam_path = std::move(bound_xam_path);
  dashboard_return = false;
  const auto ui_host = GetLaunchSettings().system_ui_host.Get();
  if (!ui_host.empty()) {
    const auto root = GetLaunchSettings().system_ui_root.Get();
    const auto path = Utf8Path(ui_host);
    if (root.empty() || !path.is_absolute() || path != configured_ui_host || !system_ui_host)
      REX_FATAL("Missing verified dashboard system UI host: {}", ui_host);
    using Install = bool (*)(rex::kernel::TitleRuntime*, const char*, const char*);
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
  auto input = GetLaunchSettings().handoff_in.Get();
  LaunchRequest request;
  if (!input.empty() &&
      (!ReadLaunchRequest(HostFile(input), request) || request.kind == LaunchRequest::WindowClose))
    REX_FATAL("Invalid supervisor launch-data record: {}", input);
  const auto profile_root = GetLaunchSettings().profile_root.Get();
  if (!profile_root.empty()) {
    auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
    if (!services) REX_FATAL("XAM title services are unavailable during launch");
    auto* user = &services->GetUserProfile();
    if (!user->InitializeLocal(Utf8Path(profile_root)))
      REX_FATAL("Invalid dashboard session profile store: {}", profile_root);
    const auto identity = user->identity();
    REXLOG_INFO("[supervisor] session profile xuid={:016X} state={} root={}",
        identity.xuid, identity.signin_state, profile_root);
    // A game launched with a signed-in account joins that account's played
    // titles; the dashboard and other system titles are not recorded.
    if (runtime.GetModuleRegistry().GetExecutable()) {
      const auto title_id = runtime.GetTitleID();
      const auto version_text = GetLaunchSettings().dashboard_version.Get();
      const auto version = ParseDashboardVersion(version_text);
      if (!version_text.empty() && !version)
        REX_FATAL("Invalid session dashboard version: {}", version_text);
      // The package the dashboard launched, when this title came from one.
      std::optional<LaunchedContent> content;
      if (!input.empty() && request.kind == LaunchRequest::Image && !request.path.empty())
        content = ReadLaunchedContent(HostFile(request.path), title_id);
      // Use the same active unlock mask exposed to the game by XAM. Reading it
      // does not grant a license or change the installed package.
      switch (DecideGamesPlayed(version, content, runtime.GetKernelSettings().license_mask.Get())) {
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
  auto* xam = REX_INTERFACE(rex::xam::IXAMTitleServices);
  if (!xam) REX_FATAL("XAM title services are unavailable during handoff");
  auto& data = xam->GetLoaderState();
  data.launch_data_present = request.data_present;
  data.launch_data = std::move(request.data);
  data.launch_flags = request.flags;
  REXLOG_INFO("[supervisor] imported {} bytes of launch data; kind={} flags={:08X} dashboard_return={}",
      data.launch_data.size(), uint32_t(request.kind), request.flags, dashboard_return);
}
void ExportLaunchRequest(rex::kernel::TitleRuntime& runtime) {
  const auto output = GetLaunchSettings().handoff_out.Get();
  if (output.empty()) return;
  auto* xam = REX_INTERFACE(rex::xam::IXAMTitleServices);
  if (!xam) REX_FATAL("XAM title services are unavailable during handoff");
  const auto& data = xam->GetLoaderState();
  LaunchRequest request;
  using Exit = rex::xam::XAMLoaderState::ExitRequest;
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
    auto host = runtime.GetFileSystem().ResolveHostPath(data.launch_path);
    if (!host) {
      REXLOG_ERROR("[supervisor] launch path has no host file: {} ({})", data.launch_path,
                   host.error().GetDescription());
      return;
    }
    auto path = rex::platform::OrdinaryPath(*host).u8string();
    request.path.assign(path.begin(), path.end());
  }
  Publish(output, request);
}
void ReleaseSessionUi(rex::kernel::TitleRuntime& runtime) {
  if (system_ui_release) {
    const auto release = std::exchange(system_ui_release, nullptr);
    release(&runtime);
  }
}
void ExportWindowClose() {
  const auto output = GetLaunchSettings().handoff_out.Get();
  if (output.empty()) return;
  LaunchRequest request; request.kind = LaunchRequest::WindowClose;
  Publish(output, request);
}
}  // namespace x360n
