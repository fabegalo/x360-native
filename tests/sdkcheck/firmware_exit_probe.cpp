// Synthetic subprocess: the unpatched HAL calls exit(0), so the parent must
// require the completion marker and handoff, not merely a zero process status.
#include "codegen/tool_runtime.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/object/object_table.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/loader_state.h>
#include "kernel/test_access.h"
#include <iostream>
#include "../../titles/common/launch_handoff.h"
#include "../../titles/common/launch_request.h"
using namespace rex::kernel;
extern "C" REX_FUNC(__imp__HalReturnToFirmware);
extern "C" REX_FUNC(__imp__HalPowerDownToBackgroundMode);
extern "C" REX_FUNC(__imp__ExRegisterTitleTerminateNotification);
namespace {
unsigned callbacks = 0;
bool callback_before_shutdown = false;
void Notify(PPCContext&, uint8_t*) {
  ++callbacks;
  callback_before_shutdown = !KernelThread::IsStopPending();
}
}
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  // "background" is the Guide power-off path, which must end as routine 5.
  const bool background = std::string(argv[1]) == "background";
  const uint32_t routine = background ? 5 : std::stoul(argv[1]);
  const std::filesystem::path root(argv[2]);
  rex::codegen::ToolApp app("firmware_exit_probe",root);
  auto tool=app.Open(root);if(!tool)return 3;
  auto& runtime=tool->GetRuntime();
  if (!(&GetFunctionDispatcher(runtime))->InitializeFunctionTable(0x8F000000,0x10000,0x8F000000,0x100000,true) ||
      !(&GetFunctionDispatcher(runtime))->SetFunction(0x8F000100, Notify)) return 4;
  StartDPCQueue(runtime);
  const auto registration = runtime.GetGuestHeap().SystemHeapAlloc(16);
  if (!registration) return 5;
  auto* data = reinterpret_cast<X_EX_TITLE_TERMINATE_REGISTRATION*>(runtime.GetMembase().GetBase()+registration);
  *data = {}; data->notification_routine = 0x8F000100; data->priority = 10;
  const auto output = root / "handoff.bin";
  if(!x360n::RegisterLaunchSettings(REX_SETTINGS()))return 10;
  rex::ScopeGuard unregister([] {REX_SETTINGS().Unregister(x360n::GetLaunchSettings());});
  if(!x360n::GetLaunchSettings().handoff_out.Set(output.string()))return 11;
  auto& loader = REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState();
  loader.launch_path = "stale.xex"; loader.launch_flags = 0x100;
  loader.launch_data_present = true; loader.launch_data = {1, 2, 3};
  bool returned = false;
  auto caller=runtime.GetThreadRegistry().LaunchHostThread("firmware exit",128*1024,[&] {
    auto& ctx = *KernelThread::GetCallingThread()->GetThreadState()->GetContext();
    ctx.r3.u64 = registration; ctx.r4.u64 = 1;
    __imp__ExRegisterTitleTerminateNotification(ctx, runtime.GetMembase().GetBase());
    ctx.r3.u64 = routine;
    if (background) __imp__HalPowerDownToBackgroundMode(ctx, runtime.GetMembase().GetBase());
    else __imp__HalReturnToFirmware(ctx, runtime.GetMembase().GetBase());
    returned = true;
    return 0;
  });
  if(!caller)return 6;
  uint64_t timeout = uint64_t(-50'000'000ll);
  if (runtime.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller,0,0,0,&timeout) != X_STATUS_SUCCESS) return 7;
  CloseOwned(runtime.GetObjectTable(),*caller);
  if (returned || callbacks != 1 || !callback_before_shutdown) return 8;
  x360n::ExportLaunchRequest(runtime);
  x360n::LaunchRequest request;
  if (!x360n::ReadLaunchRequest(output, request) || request.kind != x360n::LaunchRequest::FirmwareExit || request.flags != routine ||
      request.data_present || !request.path.empty() || !request.data.empty()) return 9;
  runtime.GetGuestHeap().SystemHeapFree(registration);
  std::cout << "firmware request completed: " << routine << "; callbacks=1; handoff=6\n";
  return 0;
}
