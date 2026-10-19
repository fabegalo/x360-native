// Synthetic subprocess: the unpatched HAL calls exit(0), so the parent must
// require the completion marker and handoff, not merely a zero process status.
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xtypes.h>
#include <rex/system/xexception.h>
#include <iostream>
#include "../../titles/common/launch_handoff.h"
#include "../../titles/common/launch_request.h"
using rex::X_STATUS;
extern "C" REX_FUNC(__imp__HalReturnToFirmware);
extern "C" REX_FUNC(__imp__ExRegisterTitleTerminateNotification);
REXCVAR_DECLARE(std::string, x360n_handoff_out);
namespace {
unsigned callbacks = 0;
bool callback_before_shutdown = false;
void Notify(PPCContext&, uint8_t*) {
  ++callbacks;
  callback_before_shutdown = !rex::Runtime::instance()->kernel_state()->is_terminating_title();
}
}
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const uint32_t routine = std::stoul(argv[1]);
  const std::filesystem::path root(argv[2]);
  rex::Runtime runtime(root, root);
  rex::RuntimeConfig config{};
  config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  if (runtime.Setup(std::move(config)) != X_STATUS_SUCCESS) return 3;
  if (!runtime.function_dispatcher()->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000) ||
      !runtime.function_dispatcher()->SetFunction(0x82010000, Notify)) return 4;
  const auto registration = runtime.memory()->SystemHeapAlloc(16);
  if (!registration) return 5;
  auto* data = runtime.memory()->TranslateVirtual<rex::system::X_EX_TITLE_TERMINATE_REGISTRATION*>(registration);
  *data = {}; data->notification_routine = 0x82010000; data->priority = 10;
  const auto output = root / "handoff.bin";
  REXCVAR_SET(x360n_handoff_out, output.string());
  auto& loader = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex")->loader_data();
  loader.launch_path = "stale.xex"; loader.launch_flags = 0x100;
  loader.launch_data_present = true; loader.launch_data = {1, 2, 3};
  bool returned = false;
  auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      runtime.kernel_state(), 128 * 1024, 0, [&] {
    auto& ctx = *rex::system::XThread::GetCurrentThread()->thread_state()->context();
    ctx.r3.u64 = registration; ctx.r4.u64 = 1;
    __imp__ExRegisterTitleTerminateNotification(ctx, runtime.memory()->virtual_membase());
    ctx.r3.u64 = routine;
    __imp__HalReturnToFirmware(ctx, runtime.memory()->virtual_membase());
    returned = true;
    return 0;
  }));
  if (caller->Create() != X_STATUS_SUCCESS) return 6;
  uint64_t timeout = uint64_t(-50'000'000ll);
  if (caller->Wait(0, 0, 0, &timeout) != X_STATUS_SUCCESS) return 7;
  if (returned || callbacks != 1 || !callback_before_shutdown) return 8;
  x360n::ExportLaunchRequest(runtime);
  x360n::LaunchRequest request;
  if (!x360n::ReadLaunchRequest(output, request) || request.kind != x360n::LaunchRequest::FirmwareExit || request.flags != routine ||
      request.data_present || !request.path.empty() || !request.data.empty()) return 9;
  runtime.memory()->SystemHeapFree(registration);
  std::cout << "firmware request completed: " << routine << "; callbacks=1; handoff=6\n";
  return 0;
}
