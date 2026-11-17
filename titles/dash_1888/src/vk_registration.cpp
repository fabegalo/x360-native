#include <rex/ppc/hook.h>
#include <rex/kernel/title_runtime.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>
namespace {
void Resource(PPCContext& ctx, uint8_t* base, uint32_t address) {
  const auto function = REX_RUNTIME()->FindFunction(address);
  REX_CHECK(function, "1888 keyboard resource callback {:08X} is unavailable", address);
  (*function)(ctx, base);
}
auto& SystemUI() {
  auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
  REX_CHECK(services, "1888 keyboard XAM services are unavailable");
  return services->GetSystemUI();
}
}
// 818D9F58 registers VK's resource even while its parent owns slot FF.
// VK is a library loaded by that parent; it must not replace its callback.
REX_HOOK_RAW(__imp__XamRegisterSysApp) {
  const auto module = ctx.r3.u32, slot = ctx.r4.u32;
  const auto callback = ctx.r5.u32, resource = ctx.r6.u32;
  if (resource != UINT32_MAX) {
    ctx.r3.u64 = resource; ctx.r4.u64 = module;
    Resource(ctx, base, 0x8192C040);
  }
  ctx.r3.u64 = SystemUI().RegisterSystemApp({module, slot, callback, resource});
}
REX_HOOK_RAW(__imp__XamUnregisterSysApp) {
  const auto module = ctx.r3.u32, slot = ctx.r4.u32, resource = ctx.r5.u32;
  if (resource != UINT32_MAX) {
    ctx.r3.u64 = resource;
    Resource(ctx, base, 0x8192C060);
  }
  ctx.r3.u64 = SystemUI().UnregisterSystemApp(module, slot);
}
