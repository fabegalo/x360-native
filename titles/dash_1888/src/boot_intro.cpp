#include "boot_intro.h"
#include <rex/logging/log_category.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/module.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/object_table.h>
#include <atomic>

namespace dash1888 {
namespace {
PPCFunc* dashboard_entry = nullptr;
std::atomic<bool> intro_running = false;
void BootThenDashboard(PPCContext& ctx, uint8_t* base) {
  auto* runtime = REX_RUNTIME();
  const auto loaded = runtime->GetModuleRegistry().LoadUserModule("game:\\bootanim.xex", false);
  if (!loaded) REX_FATAL("1888 intro: could not load the verified original module");
  auto module = runtime->GetObjectTable().Get<rex::kernel::ModuleObject>(*loaded);
  if (!module) REX_FATAL("1888 intro: could not load the verified original module");
  const auto address = module->GetExportAddress(*runtime, 1);
  const auto finish_address = module->GetExportAddress(*runtime, 2);
  auto start = runtime->FindFunction(address);
  auto finish = runtime->FindFunction(finish_address);
  if (address != 0x9801A010 || finish_address != 0x9801A068 || !start || !finish)
    REX_FATAL("1888 intro: missing or incompatible original exports/AOT mappings");
  const auto hmodule = module->hmodule_ptr;
  module = {}; // No object pin spans original guest execution.
  const PPCContext saved = ctx;
  // Export 1 preserves r3 across its CRT setup and supplies it to 980134C8,
  // whose audio path uses this hmodule for the original embedded RIFF resource.
  ctx.r3.u64 = hmodule;
  ctx.r4.u64 = 0;
  REXLOG_INFO("1888 intro: original export 1 at {:08X}, hmodule={:08X}",
              address, hmodule);
  (*start)(ctx, base);
  REXLOG_INFO("1888 intro: original export 1 returned; starting original dashboard");
  intro_running.store(false, std::memory_order_release);
  ctx = saved;
  ctx.fpscr.RestoreGuestBits(saved.fpscr.csr);
  dashboard_entry(ctx, base);
}
}
void InstallBootIntro(rex::kernel::TitleRuntime& runtime) {
  intro_running.store(true, std::memory_order_release);
  const auto executable = runtime.GetModuleRegistry().GetExecutable();
  REX_CHECK(executable, "1888 intro: executable is unavailable");
  auto module = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(*executable);
  REX_CHECK(module && module->processor_module, "1888 intro: executable image is unavailable");
  const auto entry = module->user.entry_point;
  auto* dispatcher = module->processor_module->GetFunctionDispatcher();
  dashboard_entry = dispatcher->GetFunction(entry);
  if (!dashboard_entry || dashboard_entry == BootThenDashboard ||
      !dispatcher->SetFunction(entry, BootThenDashboard))
    REX_FATAL("1888 intro: could not install bootstrap");
}
bool BootIntroRunning() { return intro_running.load(std::memory_order_acquire); }
}
