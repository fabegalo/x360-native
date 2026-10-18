// Use the generated mapping table, not a hand-written replacement. This catches
// ownership regressions which emit a callback's body but omit its entry.
#include "bounded_byte_init.h"
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xmemory.h>
#include <cstdio>

using rex::X_STATUS;

int main() {
  rex::Runtime runtime(X360N_BOUNDED_FIXTURE);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  if (runtime.Setup(PPCImageConfig, std::move(config), true) != X_STATUS_SUCCESS ||
      runtime.LoadXexImage("game:\\test.xex") != X_STATUS_SUCCESS) return 1;
  auto* callback = runtime.function_dispatcher()->GetFunction(0x82010040);
  if (!callback) { std::puts("FAIL callback missing from generated mappings"); return 2; }
  auto* base = runtime.memory()->virtual_membase();
  // Only this synthetic image is writable, for its index and mutation checks.
  if (!runtime.memory()->LookupHeap(0x82000000)->Protect(0x82000000, 0x10000,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite)) return 5;
  unsigned checks = 0;
  for (unsigned index = 0; index < 16; ++index) {
    REX_STORE_U32(0x82001000, index);
    PPCContext ctx{};
    ctx.r3.u32 = 0x82001000;
    ctx.lr = 0x12345678;
    xstart(ctx, base);
    const auto expected = index == 0 ? 11u : index == 1 ? 22u : 0x82001000u;
    if (ctx.r3.u32 != expected || ctx.lr != 0x12345678) return 3;
    ++checks;
  }
  PPCContext ctx{};
  callback(ctx, base);
  if (ctx.r3.u32 != 33) return 4;
  ++checks;
  // Runtime data still selects CTR. A post-AOT change to an external callback
  // must take the real resolver fallback rather than a frozen table/index.
  REX_STORE_U8(0x82001080, 4);
  REX_STORE_U32(0x82001000, 0);
  ctx = {};
  ctx.r3.u32 = 0x82001000;
  ctx.lr = 0x12345678;
  xstart(ctx, base);
  if (ctx.r3.u32 != 33 || ctx.ctr.u32 != 0x82010040 || ctx.lr != 0x12345678) return 6;
  ++checks;
  std::printf("PASS %u generated calls: guard, mapping, callback and mutable CTR fallback\n", checks);
}
