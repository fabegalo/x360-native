#include <catch2/catch_test_macros.hpp>
#include <array>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>
#include "synthetic_xex.h"

extern "C" void __imp__XamAlloc(PPCContext&, uint8_t*);
extern "C" void __imp__XamAllocEx(PPCContext&, uint8_t*);
extern "C" void __imp__XMemAlloc(PPCContext&, uint8_t*);
extern "C" void __imp__XamAllocSize(PPCContext&, uint8_t*);
extern "C" void __imp__XamFree(PPCContext&, uint8_t*);
using rex::X_STATUS;

TEST_CASE("XAM allocation size reports usable storage instead of leaving the input pointer",
          "[sdk][xam-allocation]") {
  x360n::test::Workdir work("x360n_xam_alloc", "size");
  rex::Runtime rt(work.dir);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* memory = rt.memory();
  auto* base = memory->virtual_membase();
  const auto output = memory->SystemHeapAlloc(4);
  REQUIRE(output);
  PPCContext ctx{};
  for (const auto size : std::array<uint32_t, 3>{32, 4097, 65537}) {
    for (unsigned allocator = 0; allocator < 6; ++allocator) {
      uint32_t pointer = 0;
      if (allocator == 0 || allocator == 5) {
        ctx.r3.u64 = allocator == 5 ? 0x20000000 : 0; ctx.r4.u64 = size; ctx.r5.u64 = output;
        __imp__XamAlloc(ctx, base);
      } else if (allocator < 4) {
        // Original image cache 81691B98 uses the system virtual class
        // 0x20000000 (816E67E8 -> 816E5F10 -> pool selector 816E5E40).
        ctx.r3.u64 = 0; ctx.r4.u64 = allocator == 3 ? 0x20000000 : allocator == 2 ? 0x01000000 : 0;
        ctx.r5.u64 = size; ctx.r6.u64 = output;
        __imp__XamAllocEx(ctx, base);
      } else {
        ctx.r3.u64 = size; ctx.r4.u64 = 0x249E0000;
        __imp__XMemAlloc(ctx, base);
        pointer = ctx.r3.u32;
      }
      if (allocator < 4 || allocator == 5) {
        REQUIRE(ctx.r3.u32 == 0);
        pointer = rex::memory::load_and_swap<uint32_t>(base + output);
      }
      REQUIRE(pointer);
      auto* heap = memory->LookupHeap(pointer);
      rex::memory::HeapAllocationInfo allocation{};
      REQUIRE(heap->QueryRegionInfo(pointer, &allocation));
      ctx.r3.u64 = pointer;
      __imp__XamAllocSize(ctx, base);
      CHECK(ctx.r3.u32 == allocation.allocation_size);
      CHECK(ctx.r3.u32 >= size);
      CHECK(ctx.r3.u32 < pointer);
      // An interior address must not expose bytes beyond the allocation's end.
      ctx.r3.u64 = pointer + 1;
      __imp__XamAllocSize(ctx, base);
      CHECK(ctx.r3.u32 == 0);
      ctx.r3.u64 = pointer;
      __imp__XamFree(ctx, base);
      ctx.r3.u64 = pointer;
      __imp__XamAllocSize(ctx, base);
      CHECK(ctx.r3.u32 == 0);
    }
  }
  for (const uint32_t invalid : {0u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
    ctx.r3.u64 = invalid;
    __imp__XamAllocSize(ctx, base);
    CHECK(ctx.r3.u32 == 0);
  }
  memory->SystemHeapFree(output);
}
