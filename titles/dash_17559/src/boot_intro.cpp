#include "boot_intro.h"
#include <rex/logging.h>
#include <rex/logging/assert.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <atomic>

namespace dash17559 {
namespace {
PPCFunc* dashboard_entry = nullptr;
std::atomic<bool> intro_running = false;
void BootThenDashboard(PPCContext& ctx, uint8_t* base) {
  auto* runtime = rex::Runtime::instance();
  auto module = runtime->kernel_state()->LoadUserModule("game:\\bootanim.xex", false);
  if (!module) REX_FATAL("17559 intro: original module could not be loaded");
  // Retail 17559 has no XEX entry point. Export 1 initializes its CRT, runs the
  // animation, and cleans up. Its first argument is used by XexGetModuleSection
  // to read the embedded 'audio' resource (0x9800E448).
  const auto address = module->GetProcAddressByOrdinal(1);
  auto* start = runtime->function_dispatcher()->GetFunction(address);
  if (!address || !start) REX_FATAL("17559 intro: export 1 has no recompiled code");
  PPCContext saved = ctx;
  const auto finish_address = module->GetProcAddressByOrdinal(2);
  auto* request_finish = runtime->function_dispatcher()->GetFunction(finish_address);
  if (!finish_address || !request_finish) REX_FATAL("17559 intro: export 2 has no recompiled code");
  // Export 2(false) requests graceful completion, NOT forced completion.
  // Original 0x9800EA10 checks its own animation time >= 6, effect state at
  // object+0xE0+0xC4, and end hold >= 4.5 before allowing cleanup/return.
  // Without the host request it intentionally holds the logo indefinitely.
  // Queue that request for this sequential intro before entering its loop;
  // leave all timing, rendering, audio and completion checks in guest code.
  ctx.r3.u64 = 0;
  request_finish(ctx, base);
  ctx = saved;
  ctx.fpscr.restoreGuestBits(saved.fpscr.csr);
  REXLOG_INFO("17559 intro: requested graceful finish via original export 2(false)");
  ctx.r3.u64 = module->hmodule_ptr();
  ctx.r4.u64 = 0; // Normal branch; flags 0x4000/0x8000 select alternate paths.
  REXLOG_INFO("17559 intro: executing original export 1 at {:08X}, hmodule={:08X}, flags=0",
              address, module->hmodule_ptr());
  start(ctx, base);
  REXLOG_INFO("17559 intro: export 1 returned {:08X}; starting original dashboard entry", ctx.r3.u32);
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
      !dispatcher->SetFunction(entry, BootThenDashboard)) {
    REX_FATAL("17559 intro: could not install optional bootstrap");
  }
}
bool BootIntroRunning() { return intro_running.load(std::memory_order_acquire); }
}
