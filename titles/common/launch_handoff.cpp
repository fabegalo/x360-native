#include "launch_handoff.h"
#include "launch_request.h"
#include <mutex>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/logging/assert.h>
#include <rex/kernel/xam/module.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>

REXCVAR_DEFINE_STRING(x360n_handoff_out, "", "Supervisor", "Per-run host handoff output");
REXCVAR_DEFINE_STRING(x360n_handoff_in, "", "Supervisor", "Previous title's launch data");

namespace x360n {
namespace {
std::mutex handoff_mutex;
void Publish(const std::string& output, const LaunchRequest& request) {
  // Guest exit and the user's close request can arrive on different threads.
  // Preserve the first completed event, without racing on the temporary file.
  std::lock_guard lock(handoff_mutex);
  if (std::filesystem::exists(output)) return;
  if (!WriteLaunchRequest(output, request))
    REXLOG_ERROR("[supervisor] could not write handoff {}", output);
  else REXLOG_INFO("[supervisor] handoff kind={}, path={}", uint32_t(request.kind), request.path);
}
}
void ImportLaunchData(rex::Runtime& runtime) {
  auto input = REXCVAR_GET(x360n_handoff_in);
  if (input.empty()) return;
  LaunchRequest request;
  if (!ReadLaunchRequest(input, request) || request.kind != LaunchRequest::Image)
    REX_FATAL("Invalid supervisor launch-data record: {}", input);
  auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  auto& data = xam->loader_data();
  data.launch_data_present = request.data_present;
  data.launch_data = std::move(request.data);
  data.launch_flags = request.flags;
  REXLOG_INFO("[supervisor] imported {} bytes of launch data", data.launch_data.size());
}
void ExportLaunchRequest(rex::Runtime& runtime) {
  const auto output = REXCVAR_GET(x360n_handoff_out);
  if (output.empty()) return;
  auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  const auto& data = xam->loader_data();
  LaunchRequest request;
  if (data.launch_path.empty()) {
    request.kind = LaunchRequest::GuestExit;
  } else {
    auto* entry = runtime.file_system()->ResolvePath(data.launch_path);
    auto* host = dynamic_cast<rex::filesystem::HostPathEntry*>(entry);
    if (!host) {
      REXLOG_ERROR("[supervisor] launch path has no host file: {}", data.launch_path);
      return;
    }
    request.path = host->host_path().string();
    request.flags = data.launch_flags;
    request.data_present = data.launch_data_present;
    request.data = data.launch_data;
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
