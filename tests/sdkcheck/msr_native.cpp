// Execute real AOT output. No game assets, graphics, or guest memory required.
#include "msr_init.h"
#include <cstdint>
#include <cstdio>
#include <initializer_list>

int main() {
  unsigned checks = 0;
  // PowerPC Book III v2.01, mtmsrd L=1: only bits 48 (EE) and 62 (RI).
  // Exercise unrelated bits, including IR, on both sides of each write.
  for (const uint32_t initial : {0u, 2u, 0x20u, 0x22u, 0x0200A032u}) {
    for (const uint32_t source : {0u, 2u, 0x20u, 0x22u}) {
      PPCContext ctx{};
      ctx.msr = initial;
      ctx.r13.u64 = source;
      ctx.r7.u64 = 0x8000 | source;
      xstart(ctx, nullptr);
      const auto entered = ctx.msr;
      sub_82010020(ctx, nullptr);  // Always balance the real runtime lock.
      const auto expected_enter = (initial & ~0x8002u) | (source & 0x8002u);
      const auto expected_exit = expected_enter | 0x8000u;
      if (entered != expected_enter || ctx.msr != expected_exit ||
          ctx.r3.u64 != expected_enter || ctx.r4.u64 != expected_exit) {
        std::printf("FAIL initial=%08X source=%08X entered=%08X restored=%08X read=%016llX/%016llX expected=%08X/%08X\n",
                    initial, source, entered, ctx.msr,
                    static_cast<unsigned long long>(ctx.r3.u64),
                    static_cast<unsigned long long>(ctx.r4.u64), expected_enter, expected_exit);
        return 1;
      }
      checks += 4;
    }
  }
  std::printf("PASS %u MSR state/readback checks through compiled AOT output\n", checks);
}
