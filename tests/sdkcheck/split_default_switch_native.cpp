// Execute the real generated large switch, including its original table loads.
#include "large_switch_init.h"
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <cstdio>

using rex::X_STATUS;

int main() {
  rex::Runtime runtime(X360N_LARGE_SWITCH_FIXTURE);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  if (runtime.Setup(PPCImageConfig, std::move(config), true) != X_STATUS_SUCCESS ||
      runtime.LoadXexImage("game:\\test.xex") != X_STATUS_SUCCESS) return 1;
  auto* base = runtime.memory()->virtual_membase();
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
