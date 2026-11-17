// Use the generated mapping table, not a hand-written replacement. This catches
// ownership regressions which emit a callback's body but omit its entry.
#include "bounded_byte_init.h"
#include "codegen/tool_runtime.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/title_paths.h>
#include <rex/kernel/guest_memory.h>
#include <cstdio>


int main() {
  rex::codegen::ToolApp app("bounded_byte_native", X360N_BOUNDED_FIXTURE);
  if (!app.Start()) return 1;
  rex::kernel::TitleRuntime runtime(kPPCImageConfig, true);
  if (!runtime.GetTitlePaths().game_root.Set(X360N_BOUNDED_FIXTURE) ||
      !runtime.GetTitlePaths().entry.Set("test.xex") || !runtime.Load()) return 1;
  auto callback = runtime.FindFunction(0x82010040);
  if (!callback) { std::puts("FAIL callback missing from generated mappings"); return 2; }
  auto* base = runtime.GetMembase().GetBase();
  // Only this synthetic image is writable, for its index and mutation checks.
  if (!runtime.GetGuestMemory().Protect(0x82000000, 0x10000,
      rex::kernel::GuestProtect::kReadWrite)) return 5;
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
  (*callback)(ctx, base);
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
