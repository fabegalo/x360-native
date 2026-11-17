#include "kernel/kernel_fixture.h"
#include "kernel/title_state.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <rex/kernel/guest_allocator.h>
TEST_CASE("XAM allocation size reports usable storage instead of leaving the "
          "input pointer",
          "[sdk][xam-allocation]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    [[maybe_unused]] auto *memory =
        rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
    auto *base = caller.GetBase();
    const auto output = caller.Alloc(4);
    auto &ctx = caller.GetContext();
    for (const auto size : std::array<uint32_t, 3>{32, 4097, 65537}) {
      for (unsigned allocator = 0; allocator < 6; ++allocator) {
        uint32_t pointer = 0;
        if (allocator == 0 || allocator == 5) {
          ctx.r3.u64 = allocator == 5 ? 0x20000000 : 0;
          ctx.r4.u64 = size;
          ctx.r5.u64 = output;
          caller.XAM("XamAlloc");
        } else if (allocator < 4) {
          // Original image cache 81691B98 uses the system virtual class
          // 0x20000000 (816E67E8 -> 816E5F10 -> pool selector 816E5E40).
          ctx.r3.u64 = 0;
          ctx.r4.u64 = allocator == 3   ? 0x20000000
                       : allocator == 2 ? 0x01000000
                                        : 0;
          ctx.r5.u64 = size;
          ctx.r6.u64 = output;
          caller.XAM("XamAllocEx");
        } else {
          ctx.r3.u64 = size;
          ctx.r4.u64 = 0x249E0000;
          caller.XAM("XMemAlloc");
          pointer = ctx.r3.u32;
        }
        if (allocator < 4 || allocator == 5) {
          REQUIRE(ctx.r3.u32 == 0);
          pointer = rex::memory::load<rex::be_u32>(base + output);
        }
        REQUIRE(pointer);
        auto *heap = memory->LookupHeap(pointer);
        rex::kernel::HeapAllocation allocation{};
        REQUIRE(heap->QueryRegionInfo(pointer, &allocation));
        ctx.r3.u64 = pointer;
        caller.XAM("XamAllocSize");
        const auto workspace_size =
            caller.GetRuntime().GetGuestWorkspace().QueryAllocationSize(
                pointer);
        CHECK(ctx.r3.u32 ==
              (workspace_size ? *workspace_size : allocation.allocation_size));
        CHECK(ctx.r3.u32 >= size);
        CHECK(ctx.r3.u32 < pointer);
        // An interior address must not expose bytes beyond the allocation's
        // end.
        ctx.r3.u64 = pointer + 1;
        caller.XAM("XamAllocSize");
        CHECK(ctx.r3.u32 == 0);
        ctx.r3.u64 = pointer;
        caller.XAM("XamFree");
        ctx.r3.u64 = pointer;
        caller.XAM("XamAllocSize");
        CHECK(ctx.r3.u32 == 0);
      }
    }
    for (const uint32_t invalid : {0u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
      ctx.r3.u64 = invalid;
      caller.XAM("XamAllocSize");
      CHECK(ctx.r3.u32 == 0);
    }
  });
}

TEST_CASE("Content cache messages receive real process virtual allocations",
          "[sdk][xam-allocation][storage-message-allocation]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    [[maybe_unused]] auto *memory =
        rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
    auto *base = caller.GetBase();
    const auto output = caller.Alloc(12);
    auto &ctx = caller.GetContext();
    for (const auto extended : {false, true}) {
      rex::memory::store<rex::be_u32>(base + output, 0x13579BDF);
      rex::memory::store<rex::be_u32>(base + output + 4, 0xFFFFFFFF);
      rex::memory::store<rex::be_u32>(base + output + 8, 0x2468ACE0);
      // Original dashboard 92263398 queues a 528-byte message this way.
      if (extended) {
        ctx.r3.u64 = 0;
        ctx.r4.u64 = 0x08000000;
        ctx.r5.u64 = 528;
        ctx.r6.u64 = output + 4;
        caller.XAM("XamAllocEx");
      } else {
        ctx.r3.u64 = 0x08000000;
        ctx.r4.u64 = 528;
        ctx.r5.u64 = output + 4;
        caller.XAM("XamAlloc");
      }
      CHECK(ctx.r3.u32 == 0);
      CHECK(rex::memory::load<rex::be_u32>(base + output) == 0x13579BDF);
      CHECK(rex::memory::load<rex::be_u32>(base + output + 8) == 0x2468ACE0);
      if (ctx.r3.u32)
        continue;
      const auto pointer = rex::memory::load<rex::be_u32>(base + output + 4);
      REQUIRE(pointer);
      std::memset(base + pointer, 0x7E, 528);
      CHECK(base[pointer] == 0x7E);
      CHECK(base[pointer + 527] == 0x7E);
      ctx.r3.u64 = pointer;
      caller.XAM("XamAllocSize");
      CHECK(ctx.r3.u32 >= 528);
      ctx.r3.u64 = pointer;
      caller.XAM("XamFree");
      CHECK(ctx.r3.u32 == 0);
      ctx.r3.u64 = pointer;
      caller.XAM("XamAllocSize");
      CHECK(ctx.r3.u32 == 0);
    }
    for (const auto flags : {0x08000001u, 0x01000001u}) {
      rex::memory::store<rex::be_u32>(base + output + 4, 0xFFFFFFFF);
      ctx.r3.u64 = 0;
      ctx.r4.u64 = flags;
      ctx.r5.u64 = 528;
      ctx.r6.u64 = output + 4;
      caller.XAM("XamAllocEx");
      CHECK(ctx.r3.u32 == 0x80004001);
      CHECK(rex::memory::load<rex::be_u32>(base + output + 4) == 0);
    }
  });
}

TEST_CASE("Sign-in initialization receives a virtual allocation for its "
          "original class",
          "[sdk][xam-allocation][signin-allocation]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    [[maybe_unused]] auto *memory =
        rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
    auto *base = caller.GetBase();
    const auto output = caller.Alloc(4);
    auto &ctx = caller.GetContext();
    // The original 17559 signin.xex initializer requests class 0x24400000. It
    // has no physical bit, so 816E67E8 takes the virtual path (816E5F10 ->
    // 816E63D8); 816E5E40 selects pool 6 from bit 0x00400000, and 816E5D58
    // zeroes it, since 0x00100000 is clear. Without it the Guide's Sign In got
    // 80004001.
    for (const auto size : {64u, 4097u}) {
      rex::memory::store<rex::be_u32>(base + output, 0xFFFFFFFF);
      ctx.r3.u64 = 0;
      ctx.r4.u64 = 0x24400000;
      ctx.r5.u64 = size;
      ctx.r6.u64 = output;
      caller.XAM("XamAllocEx");
      REQUIRE(ctx.r3.u32 == 0);
      const auto pointer = rex::memory::load<rex::be_u32>(base + output);
      REQUIRE(pointer);
      CHECK(base[pointer] == 0);
      CHECK(base[pointer + size - 1] == 0);
      std::memset(base + pointer, 0x5A, size);
      CHECK(base[pointer + size - 1] == 0x5A);
      ctx.r3.u64 = pointer;
      caller.XAM("XamAllocSize");
      CHECK(ctx.r3.u32 >= size);
      ctx.r3.u64 = pointer;
      caller.XAM("XamFree");
      CHECK(ctx.r3.u32 == 0);
      ctx.r3.u64 = pointer;
      caller.XAM("XamAllocSize");
      CHECK(ctx.r3.u32 == 0);
    }
    // Caller-selected virtual pool bytes remain unsupported. Physical
    // allocation is now implemented upstream and is tested separately below.
    for (const auto flags : {0x244000F2u, 0x20000001u}) {
      rex::memory::store<rex::be_u32>(base + output, 0xFFFFFFFF);
      ctx.r3.u64 = 0;
      ctx.r4.u64 = flags;
      ctx.r5.u64 = 64;
      ctx.r6.u64 = output;
      caller.XAM("XamAllocEx");
      CHECK(ctx.r3.u32 == 0x80004001);
      CHECK(rex::memory::load<rex::be_u32>(base + output) == 0);
    }
  });
}

TEST_CASE("Official physical XAM allocations retain size, alignment and "
          "release contracts",
          "[sdk][xam-allocation][physical]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    const auto output = caller.Alloc(4);
    for (uint32_t flags : {0x40000000u, 0x64400000u}) {
      const uint32_t attributes = 0x40000000u | (2u << 28) | (0xEu << 24);
      REQUIRE(caller.XAM("XamAllocEx", {attributes, flags, 4097, output}) == 0);
      const auto pointer = caller.Load32(output);
      REQUIRE(pointer >= 0xE0000000u);
      CHECK((pointer - 0xE0000000u + 0x1000) % 0x4000 == 0);
      CHECK(caller.Load8(pointer) == 0);
      CHECK(caller.Load8(pointer + 4096) == 0);
      const auto size = caller.XAM("XamAllocSize", {pointer});
      CHECK(size >= 4097);
      CHECK(size <= 0x4000);
      CHECK(caller.XAM("XamAllocSize", {pointer + 1}) == 0);
      CHECK(caller.XAM("XamFree", {pointer}) == 0);
      CHECK(caller.XAM("XamAllocSize", {pointer}) == 0);
    }
  });
}
