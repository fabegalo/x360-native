// Execute the real generated large switch, including its original table loads.
#include "large_switch_init.h"
#include "codegen/tool_runtime.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/title_paths.h>
#include <rex/kernel/guest_memory.h>
#include <cstdio>


int main() {
  rex::codegen::ToolApp app("split_default_switch_native", X360N_LARGE_SWITCH_FIXTURE);
  if (!app.Start()) return 1;
  rex::kernel::TitleRuntime runtime(kPPCImageConfig, true);
  if (!runtime.GetTitlePaths().game_root.Set(X360N_LARGE_SWITCH_FIXTURE) ||
      !runtime.GetTitlePaths().entry.Set("test.xex") || !runtime.Load()) return 1;
  auto* base = runtime.GetMembase().GetBase();
  for (uint32_t index = 0; index < 65536; ++index) {
    PPCContext ctx{};
    ctx.r3.u64 = 0x1122334400000000ull | index;
    ctx.r31.u64 = 0x8877665544332211ull;
    ctx.lr = 0x12345678;
    ctx.ctr.u64 = 0xAABBCCDD;
    xstart(ctx, base);
    const uint32_t expected = index < 512 ? 11 : index < 642 ? 22 : index == 642 ? 33 : 44;
    const uint32_t target = index < 512 ? 0x8201002C : index < 642 ? 0x82010034 :
                            index == 642 ? 0x8201003C : 0xAABBCCDD;
    if (ctx.r3.u64 != expected || ctx.ctr.u64 != target || ctx.lr != 0x12345678 ||
        ctx.r31.u64 != 0x8877665544332211ull) {
      std::printf("FAIL index=%u result=%llu ctr=%llX expected=%u/%X\n", index,
                  static_cast<unsigned long long>(ctx.r3.u64),
                  static_cast<unsigned long long>(ctx.ctr.u64), expected, target);
      return 2;
    }
  }
  std::puts("PASS 65536 indices through the compiled large switch and default guard");
}
