#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/kernel/xam/module.h>

// 17559 XAM 816BC9D0 registers the resource even when the foreground
// slot already has an app. vk's library exports share the parent's scene;
// registering its resources must not replace the CreateProfile callback.
extern "C" REX_FUNC(__imp__XamRegisterSysApp) {
  const auto module = ctx.r3.u32, slot = ctx.r4.u32;
  const auto callback = ctx.r5.u32, resource = ctx.r6.u32;
  if (resource != UINT32_MAX) {
    ctx.r3.u64 = resource; ctx.r4.u64 = module;
    REX_KERNEL_STATE()->function_dispatcher()->GetFunction(0x817DCBB8)(ctx, base);
  }
  ctx.r3.u64 = REX_KERNEL_STATE()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex")
      ->RegisterSystemApp({module, slot, callback, resource});
}
extern "C" REX_FUNC(__imp__XamUnregisterSysApp) {
  const auto module = ctx.r3.u32, slot = ctx.r4.u32, resource = ctx.r5.u32;
  if (resource != UINT32_MAX) {
    ctx.r3.u64 = resource;
    REX_KERNEL_STATE()->function_dispatcher()->GetFunction(0x817DCBD8)(ctx, base);
  }
  ctx.r3.u64 = REX_KERNEL_STATE()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex")
      ->UnregisterSystemApp(module, slot);
}
