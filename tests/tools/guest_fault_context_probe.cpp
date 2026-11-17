// Synthetic fault fixture; no originals, windows, input, profiles or gameplay.
#include "../../titles/common/guest_fault_context.h"
#include "codegen/tool_runtime.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/guest_memory.h>
#include <rex/kernel/heap.h>
#include <string_view>
#include <cstring>
#include <windows.h>

namespace {
bool Handled(rex::platform::Exception*, void*) { return true; }
}
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const auto root = std::filesystem::path(argv[1]);
  const std::string_view mode(argv[2]);
  rex::codegen::ToolApp app("guest_fault_probe", root);
  auto tool = app.Open(root);
  if (!tool) return 3;
  auto& runtime = tool->GetRuntime();
  if (mode == "handled") rex::platform::ExceptionHandler::Install(Handled, nullptr);
  x360n::GuestFaultContext diagnostic;
  if (mode != "disabled" && !diagnostic.Start(runtime, root / "fault.ppc.txt")) return 4;
  if (mode == "stopped") diagnostic.Stop();
  if (mode == "resumed") rex::platform::ExceptionHandler::Install(Handled, nullptr);
  auto fault = [&] {
    if (mode == "handled" || mode == "resumed") {
      const ULONG_PTR args[]{0, reinterpret_cast<ULONG_PTR>(runtime.GetMembase().GetBase()) + 5};
      for (unsigned i = 0; i < (mode == "resumed" ? 20u : 1u); ++i)
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 2, args);
    } else {
      // Volatile keeps the actual access, so the OS debug API must still see
      // a second-chance AV at guest address five after the observer returns.
      const auto value = *static_cast<volatile uint8_t*>(runtime.GetMembase().GetBase() + 5);
      (void)value;
    }
    return 0;
  };
  if (mode == "host") return fault();
  auto caller = runtime.GetThreadRegistry().LaunchHostThread("fault probe", 128 * 1024, [&] {
        auto* ctx = rex::kernel::KernelThread::GetCallingThread()->GetThreadState()->GetContext();
        ctx->r0.u64 = 0x0011223344556677ull;
        ctx->r3.u64 = 0x123456789ABCDEF0ull;
        ctx->r31.u64 = 0xFFEEDDCCBBAA9988ull;
        const auto snapshot = runtime.GetGuestHeap().SystemHeapAlloc(128);
        if (!snapshot) return 8;
        std::memset(runtime.GetMembase().GetBase() + snapshot, 0, 128);
        rex::memory::store_and_swap<uint32_t>(runtime.GetMembase().GetBase() + snapshot, 0xCAFEBABE);
        rex::memory::store_and_swap<uint32_t>(runtime.GetMembase().GetBase() + snapshot + 4, snapshot + 64);
        rex::memory::store_and_swap<uint32_t>(runtime.GetMembase().GetBase() + snapshot + 64, 0xDEADC0DE);
        ctx->r28.u64 = snapshot;
        ctx->r29.u64 = 5;  // Unreadable memory must not trigger another fault.
        ctx->lr = 0x90105000;
        ctx->ctr.u64 = 0x913F847C;
        ctx->last_indirect_target = 0x90116C88;
        ctx->cr0.SetRaw(0xA);
        ctx->cr7.SetRaw(0x5);
        const auto result = fault();
        return ctx->r0.u64 == 0x0011223344556677ull && ctx->r3.u64 == 0x123456789ABCDEF0ull &&
            ctx->lr == 0x90105000 && ctx->cr0.GetRaw() == 0xA ? result : 7;
      });
  if (!caller) return 5;
  uint64_t timeout = uint64_t(-50'000'000ll);
  if (runtime.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller, 0, 0, 0, &timeout)) ExitProcess(6);
  diagnostic.Stop();
  if (mode == "handled" || mode == "resumed") rex::platform::ExceptionHandler::Uninstall(Handled, nullptr);
  return 0;
}
