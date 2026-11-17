#include <rex/ppc/hook.h>
#include <rex/kernel/title_runtime.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>

namespace {
void Resource(PPCContext& ctx, uint8_t* base, uint32_t address) {
  auto function = REX_RUNTIME()->FindFunction(address);
  REX_CHECK(function, "17559 keyboard resource function {:08X} unavailable", address);
  (*function)(ctx, base);
}
rex::xam::SystemUIState& SystemUI() {
  auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
  REX_CHECK(services, "17559 keyboard XAM services unavailable");
  return services->GetSystemUI();
}
}

// 17559 XAM 816BC9D0 registers the resource even when the foreground
// slot already has an app. vk's library exports share the parent's scene;
// registering its resources must not replace the CreateProfile callback.
REX_HOOK_RAW(__imp__XamRegisterSysApp) {
  const auto module = ctx.r3.u32, slot = ctx.r4.u32;
  const auto callback = ctx.r5.u32, resource = ctx.r6.u32;
  if (resource != UINT32_MAX) {
    ctx.r3.u64 = resource; ctx.r4.u64 = module;
    Resource(ctx, base, 0x817DCBB8);
  }
  ctx.r3.u64 = SystemUI().RegisterSystemApp({module, slot, callback, resource});
}
REX_HOOK_RAW(__imp__XamUnregisterSysApp) {
  const auto module = ctx.r3.u32, slot = ctx.r4.u32, resource = ctx.r5.u32;
  if (resource != UINT32_MAX) {
    ctx.r3.u64 = resource;
    Resource(ctx, base, 0x817DCBD8);
  }
  ctx.r3.u64 = SystemUI().UnregisterSystemApp(module, slot);
}
