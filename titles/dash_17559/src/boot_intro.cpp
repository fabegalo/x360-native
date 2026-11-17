#include "boot_intro.h"
#include <rex/logging/log_category.h>
#include <rex/assert.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/module.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/object_table.h>
#include <atomic>

namespace dash17559 {
namespace {
PPCFunc* dashboard_entry = nullptr;
std::atomic<bool> intro_running = false;
void BootThenDashboard(PPCContext& ctx, uint8_t* base) {
  auto* runtime = REX_RUNTIME();
  REX_CHECK(runtime, "17559 intro: no loaded title runtime");
  auto loaded = runtime->GetModuleRegistry().LoadUserModule("game:\\bootanim.xex", false);
  REX_CHECK(loaded, "17559 intro: original module could not be loaded");
  auto module = runtime->GetObjectTable().Get<rex::kernel::ModuleObject>(*loaded);
  REX_CHECK(module, "17559 intro: original module handle is invalid");
  // Retail 17559 has no XEX entry point. Export 1 initializes its CRT, runs the
  // animation, and cleans up. Its first argument is used by XexGetModuleSection
  // to read the embedded 'audio' resource (0x9800E448).
  const auto address = module->GetExportAddress(*runtime, 1);
  auto start = runtime->FindFunction(address);
  REX_CHECK(address && start, "17559 intro: export 1 has no recompiled code");
  PPCContext saved = ctx;
  const auto finish_address = module->GetExportAddress(*runtime, 2);
  auto request_finish = runtime->FindFunction(finish_address);
  REX_CHECK(finish_address && request_finish, "17559 intro: export 2 has no recompiled code");
  const auto hmodule = module->hmodule_ptr;
  // The module registry owns the load reference; no object pin spans guest execution.
  module = {};
  // Export 2(false) requests graceful completion, NOT forced completion.
  // Original 0x9800EA10 checks its own animation time >= 6, effect state at
  // object+0xE0+0xC4, and end hold >= 4.5 before allowing cleanup/return.
  // Without the host request it intentionally holds the logo indefinitely.
  // Queue that request for this sequential intro before entering its loop;
  // leave all timing, rendering, audio and completion checks in guest code.
  ctx.r3.u64 = 0;
  (*request_finish)(ctx, base);
  ctx = saved;
  ctx.fpscr.RestoreGuestBits(saved.fpscr.csr);
  REXLOG_INFO("17559 intro: requested graceful finish via original export 2(false)");
  ctx.r3.u64 = hmodule;
  ctx.r4.u64 = 0; // Normal branch; flags 0x4000/0x8000 select alternate paths.
  REXLOG_INFO("17559 intro: executing original export 1 at {:08X}, hmodule={:08X}, flags=0",
              address, hmodule);
  (*start)(ctx, base);
  REXLOG_INFO("17559 intro: export 1 returned {:08X}; starting original dashboard entry", ctx.r3.u32);
  intro_running.store(false, std::memory_order_release);
  ctx = saved;
  ctx.fpscr.RestoreGuestBits(saved.fpscr.csr);
  dashboard_entry(ctx, base);
}
}
void InstallBootIntro(rex::kernel::TitleRuntime& runtime) {
  const auto executable = runtime.GetModuleRegistry().GetExecutable();
  REX_CHECK(executable, "17559 intro: no executable module");
  auto module = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(*executable);
  REX_CHECK(module && module->processor_module, "17559 intro: executable has no image");
  const auto entry = module->user.entry_point;
  auto* dispatcher = module->processor_module->GetFunctionDispatcher();
  REX_CHECK(dispatcher, "17559 intro: executable has no dispatcher");
  dashboard_entry = dispatcher->GetFunction(entry);
  if (!dashboard_entry || dashboard_entry == BootThenDashboard ||
      !dispatcher->SetFunction(entry, BootThenDashboard)) {
    REX_CHECK(false, "17559 intro: could not install optional bootstrap");
  }
  intro_running.store(true, std::memory_order_release);
}
bool BootIntroRunning() { return intro_running.load(std::memory_order_acquire); }
}
