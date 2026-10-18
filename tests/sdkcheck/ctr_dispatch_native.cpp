// Executes the synthetic XEX's actual generated C++, including host dispatch.
#include "byte_ctr_init.h"
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <cstdio>

using rex::X_STATUS;

int main() {
  rex::Runtime runtime(X360N_CTR_FIXTURE);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  const PPCFuncMapping functions[] = {
      {0x82010000, xstart}, {0x8201003C, sub_8201003C}, {0, nullptr}};
  const rex::PPCImageInfo image{0x82010000, 0x200, 0x82000000, 0x20000, functions};
  if (runtime.Setup(image, std::move(config), true) != X_STATUS_SUCCESS ||
      runtime.LoadXexImage("game:\\test.xex") != X_STATUS_SUCCESS) return 1;
  auto* base = runtime.memory()->virtual_membase();
  // Only this synthetic fixture is made writable for the post-AOT mutation test.
  if (!runtime.memory()->LookupHeap(0x82000000)->Protect(0x82000000, 0x10000,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite)) return 3;
  unsigned checks = 0;
  // Include indices beyond both the original two slots and the old 512-slot
  // guess. Mutate table data after AOT: dispatch must use the byte actually read.
  for (unsigned index = 0; index < 1024; ++index) {
    const uint8_t values[] = {0, 2, 4};
    const uint32_t expected[] = {11, 22, 44};
    for (unsigned pass = 0; pass < 3; ++pass) {
      const unsigned which = (index + pass) % 3;
      REX_STORE_U32(0x82001000, index);
      REX_STORE_U8(0x82001200 + index, values[which]);
      PPCContext ctx{};
      ctx.r3.u64 = 0x82001000;
      ctx.lr = 0x12345678;
      ctx.r31.u64 = 0x1122334455667788;
      xstart(ctx, base);
      if (ctx.r3.u32 != expected[which] || ctx.lr != 0x12345678 ||
          ctx.r31.u64 != 0x1122334455667788 ||
          ctx.ctr.u32 != 0x8201002C + values[which] * 4) {
        std::printf("FAIL index=%u pass=%u r3=%u ctr=%08X\n", index, pass, ctx.r3.u32, ctx.ctr.u32);
        return 2;
      }
      ++checks;
    }
  }
  std::printf("PASS %u calls through generated PPC and the real host resolver\n", checks);
}
