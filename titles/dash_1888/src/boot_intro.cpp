#include "boot_intro.h"
#include <rex/logging.h>
#include <rex/logging/assert.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <atomic>

namespace dash1888 {
namespace {
PPCFunc* dashboard_entry = nullptr;
std::atomic<bool> intro_running = false;
void BootThenDashboard(PPCContext& ctx, uint8_t* base) {
  auto* runtime = rex::Runtime::instance();
  auto module = runtime->kernel_state()->LoadUserModule("game:\\bootanim.xex", false);
  if (!module) REX_FATAL("1888 intro: could not load the verified original module");
  const auto address = module->GetProcAddressByOrdinal(1);
  const auto finish_address = module->GetProcAddressByOrdinal(2);
  auto* start = runtime->function_dispatcher()->GetFunction(address);
  auto* finish = runtime->function_dispatcher()->GetFunction(finish_address);
  if (address != 0x9801A010 || finish_address != 0x9801A068 || !start || !finish)
    REX_FATAL("1888 intro: missing or incompatible original exports/AOT mappings");
  const PPCContext saved = ctx;
  // Export 1 preserves r3 across its CRT setup and supplies it to 980134C8,
  // whose audio path uses this hmodule for the original embedded RIFF resource.
  ctx.r3.u64 = module->hmodule_ptr();
  ctx.r4.u64 = 0;
  REXLOG_INFO("1888 intro: original export 1 at {:08X}, hmodule={:08X}",
              address, module->hmodule_ptr());
  start(ctx, base);
  REXLOG_INFO("1888 intro: original export 1 returned; starting original dashboard");
  intro_running.store(false, std::memory_order_release);
  ctx = saved;
  ctx.fpscr.restoreGuestBits(saved.fpscr.csr);
  dashboard_entry(ctx, base);
}
}
void InstallBootIntro(rex::Runtime& runtime) {
  intro_running.store(true, std::memory_order_release);
  const auto entry = runtime.kernel_state()->GetExecutableModule()->entry_point();
  auto* dispatcher = runtime.function_dispatcher();
  dashboard_entry = dispatcher->GetFunction(entry);
  if (!dashboard_entry || dashboard_entry == BootThenDashboard ||
      !dispatcher->SetFunction(entry, BootThenDashboard))
    REX_FATAL("1888 intro: could not install bootstrap");
}
bool BootIntroRunning() { return intro_running.load(std::memory_order_acquire); }
}
