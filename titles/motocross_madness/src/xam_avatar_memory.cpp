// XAM 17559's internal XMem entry points used by the original texture codec.
// Small BC mip levels enter the general format converter, which allocates
// temporary rows. Delegate these allocations to the existing SDK XMem exports
// (patch 0032) instead of starting the original XAM global heap manager.
// Strong hooks replace the generated weak aliases, including direct AOT calls.
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>

REX_EXTERN(__imp__XMemAlloc);
REX_EXTERN(__imp__XMemFree);

extern "C" REX_FUNC(sub_816E69E0) {
  const auto size = ctx.r3.u32, attributes = ctx.r4.u32;
  __imp__XMemAlloc(ctx, base);

  if (!ctx.r3.u32)
    REXLOG_ERROR("avatar-memory: XMemAlloc size={} attributes={:08X} failed",
                 size, attributes);
}
extern "C" REX_FUNC(sub_816E6AB0) {
  __imp__XMemFree(ctx, base);
}

#include <rex/memory.h>
REX_EXTERN(__imp__sub_81A1CE90);
extern "C" REX_FUNC(sub_81A1CE90) {
  const auto width=ctx.r5.u32,height=ctx.r6.u32,format=ctx.r7.u32;
  const auto source=rex::memory::load_and_swap<uint32_t>(base+ctx.r1.u32+0x54);
  __imp__sub_81A1CE90(ctx,base);
  if(ctx.r3.s32<0) REXLOG_ERROR("avatar-codec: convert {}x{} {:08X}->{:08X} failed {:08X}",width,height,source,format,ctx.r3.u32);
}

// Original nothrow operator new selects the XAM pool then calls 816E63D8.
// Keep the byte count and null-on-failure contract, but let SDK XMem own it.
// Its matching delete (816E6B10) already tail-calls the XMemFree hook above.
extern "C" REX_FUNC(sub_816E6B18) {
  ctx.r4.u64 = 0x01000500u;
  __imp__XMemAlloc(ctx, base);
}