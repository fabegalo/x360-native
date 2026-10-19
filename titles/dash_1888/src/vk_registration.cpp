#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

// 818D9F58 registers VK's resource even while its parent owns slot FF.
// VK is a library loaded by that parent; it must not replace its callback.
extern "C" REX_FUNC(__imp__XamRegisterSysApp) {
  const auto module = ctx.r3.u32, slot = ctx.r4.u32;
  const auto callback = ctx.r5.u32, resource = ctx.r6.u32;
  if (resource != UINT32_MAX) {
    ctx.r3.u64 = resource; ctx.r4.u64 = module;
    REX_KERNEL_STATE()->function_dispatcher()->GetFunction(0x8192C040)(ctx, base);
  }
  ctx.r3.u64 = REX_KERNEL_STATE()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex")
      ->RegisterSystemApp({module, slot, callback, resource});
}
extern "C" REX_FUNC(__imp__XamUnregisterSysApp) {
  const auto module = ctx.r3.u32, slot = ctx.r4.u32, resource = ctx.r5.u32;
  if (resource != UINT32_MAX) {
    ctx.r3.u64 = resource;
    REX_KERNEL_STATE()->function_dispatcher()->GetFunction(0x8192C060)(ctx, base);
  }
  ctx.r3.u64 = REX_KERNEL_STATE()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex")
      ->UnregisterSystemApp(module, slot);
}
