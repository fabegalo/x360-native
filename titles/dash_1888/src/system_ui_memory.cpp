#include <algorithm>
#include <cstring>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

// Defined in this XAM DLL, which owns the hooked keystroke boundary.
REXCVAR_DEFINE_BOOL(dash1888_ui_input_trace, false, "Diagnostics",
                   "Log each keystroke delivered to the original system UI (opt-in)");

// Original 1888 private allocation boundaries. The full XAM heap/task manager
// is not started; virtual allocations share the SDK's guest system heap.
// Physical command memory retains the original allocator and protection flags.
extern "C" REX_FUNC(__imp__sub_818E8870);
extern "C" REX_FUNC(sub_818E8870) {
  if (ctx.r4.u32 & 0x80000000u) __imp__sub_818E8870(ctx, base);
  else ctx.r3.u64 = REX_KERNEL_MEMORY()->SystemHeapAlloc(std::max(1u, ctx.r3.u32), 0x20);
}
extern "C" REX_FUNC(sub_818E8900) {
  ctx.r3.u64 = REX_KERNEL_MEMORY()->SystemHeapAlloc(std::max(1u, ctx.r3.u32), 0x20);
}
extern "C" REX_FUNC(sub_818E8940) { sub_818E8900(ctx, base); }
extern "C" REX_FUNC(__imp__sub_818E8980);
extern "C" REX_FUNC(sub_818E8980) {
  if (ctx.r3.u32 >= 0xA0000000u) __imp__sub_818E8980(ctx, base);
  else if (ctx.r3.u32) REX_KERNEL_MEMORY()->SystemHeapFree(ctx.r3.u32);
}
extern "C" REX_FUNC(__imp__XamAlloc);
extern "C" REX_FUNC(__imp__XamAllocEx);
extern "C" REX_FUNC(__imp__XamFree);
extern "C" REX_FUNC(__imp__XamAllocSize);
extern "C" REX_FUNC(__imp__sub_818E8608);
extern "C" REX_FUNC(__imp__sub_818E8680);
extern "C" REX_FUNC(sub_818E8580) { __imp__XamAlloc(ctx, base); }
extern "C" REX_FUNC(sub_818E8608) {
  if (ctx.r4.u32 & 0x40000000u) __imp__sub_818E8608(ctx, base);
  else __imp__XamAllocEx(ctx, base);
}
extern "C" REX_FUNC(sub_818E8680) {
  if (ctx.r3.u32 >= 0xA0000000u) __imp__sub_818E8680(ctx, base);
  else __imp__XamFree(ctx, base);
}
extern "C" REX_FUNC(__imp__sub_818E8770);
extern "C" REX_FUNC(sub_818E8770) {
  if (ctx.r3.u32 >= 0xA0000000u) __imp__sub_818E8770(ctx, base);
  else __imp__XamAllocSize(ctx, base);
}

// Internal XAM calls use the same persistent account and controller services
// as imported calls. Addresses are from this exact XAM's export table.
#define SERVICE(address, name) \
  extern "C" REX_FUNC(__imp__##name); \
  extern "C" REX_FUNC(sub_##address) { __imp__##name(ctx, base); }
SERVICE(818E2498, XamUserGetXUID)
SERVICE(818E2538, XamUserGetIndexFromXUID)
SERVICE(818E25C8, XamUserGetGamerTag)
SERVICE(818E4B60, XamUserGetName)
SERVICE(818E2730, XamUserGetSigninState)
SERVICE(818D6CB8, XamUserReadProfileSettings)
SERVICE(818C0598, XamReadTile)
SERVICE(818ECFE8, XGetLanguage)
SERVICE(818ECE68, XamInputGetKeystrokeHud)
// Original 1888 task ABI: callback, context, two-word attributes, output handle
// (818D98D0). Keep internal UI tasks on the same real SDK task service used
// by imported calls; the original XAM scheduler has not been started.
SERVICE(818D98D0, XamTaskSchedule)
SERVICE(818D9600, XamTaskShouldExit)
SERVICE(818D77C0, XamTaskCloseHandle)
SERVICE(818D95F8, XamTaskCancel)
SERVICE(818D77D8, XamTaskWaitOnCompletion)
SERVICE(818D9618, XamTaskGetCurrentTask)
// The original listener factory (818D49F0) has the SDK's internal ABI:
// 64-bit mask, system flag, maximum version. Both producers and consumers
// must use the same waitable notification queues, including private IDs.
SERVICE(818D49F0, XamNotifyCreateListenerInternal)
SERVICE(818D46A0, XNotifyGetNext)
SERVICE(818D4CF8, XNotifyBroadcast)
#undef SERVICE

extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHudEx);
extern "C" REX_FUNC(__imp__sub_818ECC48);
extern "C" REX_FUNC(sub_818ECC48) {
  // 818D00C8 packs the system queue's user selector into flags, not r3.
  // Keep the original path for other queues, and the SDK latch for HUD keys.
  const uint32_t flags = ctx.r4.u32, output = ctx.r5.u32, caller = uint32_t(ctx.lr);
  if (flags & 0x40000000u) {
    ctx.r3.u64 = flags & 0xFF;
    __imp__XamInputGetKeystrokeHudEx(ctx, base);
  } else __imp__sub_818ECC48(ctx, base);
  if (REXCVAR_GET(dash1888_ui_input_trace) && !ctx.r3.u32 && output) {
    const auto* stroke = base + output;
    REXLOG_INFO("1888 diagnostic: UI keystroke vk={:04X} flags={:04X} user={} queue={:08X} lr={:08X}",
                rex::memory::load_and_swap<uint16_t>(stroke),
                rex::memory::load_and_swap<uint16_t>(stroke + 4), stroke[6], flags, caller);
  }
}
