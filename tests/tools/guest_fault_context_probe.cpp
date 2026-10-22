// Synthetic fault fixture; no originals, windows, input, profiles or gameplay.
#include "../../titles/common/guest_fault_context.h"
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/xthread.h>
#include <rex/system/thread_state.h>
#include <string_view>
#include <windows.h>

namespace {
bool Handled(rex::arch::Exception*, void*) { return true; }
}
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const auto root = std::filesystem::path(argv[1]);
  const std::string_view mode(argv[2]);
  rex::Runtime runtime(root, root);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  if (runtime.Setup(std::move(config))) return 3;
  if (mode == "handled") rex::arch::ExceptionHandler::Install(Handled, nullptr);
  x360n::GuestFaultContext diagnostic;
  if (mode != "disabled" && !diagnostic.Start(*runtime.memory(), root / "fault.ppc.txt")) return 4;
  if (mode == "stopped") diagnostic.Stop();
  if (mode == "resumed") rex::arch::ExceptionHandler::Install(Handled, nullptr);
  auto fault = [&] {
    if (mode == "handled" || mode == "resumed") {
      const ULONG_PTR args[]{0, reinterpret_cast<ULONG_PTR>(runtime.memory()->virtual_membase()) + 5};
      for (unsigned i = 0; i < (mode == "resumed" ? 20u : 1u); ++i)
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 2, args);
    } else {
      // Volatile keeps the actual access, so the OS debug API must still see
      // a second-chance AV at guest address five after the observer returns.
      const auto value = *static_cast<volatile uint8_t*>(runtime.memory()->TranslateVirtual(5));
      (void)value;
    }
    return 0;
  };
  if (mode == "host") return fault();
  auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      runtime.kernel_state(), 128 * 1024, 0, [&] {
        auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
        ctx->r0.u64 = 0x0011223344556677ull;
        ctx->r3.u64 = 0x123456789ABCDEF0ull;
        ctx->r31.u64 = 0xFFEEDDCCBBAA9988ull;
        ctx->lr = 0x90105000;
        ctx->ctr.u64 = 0x913F847C;
        ctx->last_indirect_target = 0x90116C88;
        ctx->cr0.set_raw(0xA);
        ctx->cr7.set_raw(0x5);
        const auto result = fault();
        return ctx->r0.u64 == 0x0011223344556677ull && ctx->r3.u64 == 0x123456789ABCDEF0ull &&
            ctx->lr == 0x90105000 && ctx->cr0.raw() == 0xA ? result : 7;
      }));
  if (caller->Create()) return 5;
  uint64_t timeout = uint64_t(-50'000'000ll);
  if (caller->Wait(0, 0, 0, &timeout)) ExitProcess(6);
  diagnostic.Stop();
  if (mode == "handled" || mode == "resumed") rex::arch::ExceptionHandler::Uninstall(Handled, nullptr);
  return 0;
}
