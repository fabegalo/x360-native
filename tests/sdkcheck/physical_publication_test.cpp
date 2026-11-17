#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <rex/kernel/heap.h>
#include "kernel/kernel_fixture.h"
#include <rex/kernel/title_runtime.h>

TEST_CASE("Physical write publication invalidates aliases after data becomes visible",
          "[sdk][gpu][shader-fetch][physical-publication][service-contracts]") {
  const auto page_size = GENERATE(4096u, 65536u);
  rex::test::KernelFixture fixture;
  // Tools use the same title-owned services before Launch attaches graphics.
  auto& runtime = fixture.GetRuntime();
  CHECK(&runtime.GetPhysicalMemory() == &runtime.GetPhysicalMemory());
  CHECK(&runtime.GetPhysicalWriteWatch() == &runtime.GetPhysicalWriteWatch());
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& context = caller.GetRuntime().GetPhysicalMemory();
    auto& watches = caller.GetRuntime().GetPhysicalWriteWatch();
    CHECK(dynamic_cast<void*>(&context) == dynamic_cast<void*>(&watches));
    CHECK(dynamic_cast<void*>(&context) == dynamic_cast<void*>(&runtime.GetGuestInterrupts()));
    constexpr uint32_t address = 0x01000000;
    const auto allocation = runtime.GetGuestHeap().AllocPhysical(page_size, page_size,
        rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite,
        address, address + page_size - 1, page_size);
    REQUIRE(allocation);
    CHECK(context.IsPhysicalRangeAllocated({address, page_size}));
    if (page_size == 4096) {
      CHECK_FALSE(context.IsPhysicalRangeAllocated({address + 4096, 4096}));
      const auto adjacent = runtime.GetGuestHeap().AllocPhysical(4096, 4096,
          rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite,
          address + 4096, address + 8191, 4096);
      REQUIRE(adjacent);
      CHECK(context.IsPhysicalRangeAllocated({address, 8192}));
    }
    struct Listener final : rex::IPhysicalWriteListener {
      rex::IPhysicalMemory& context;
      std::vector<uint32_t> observed;
      explicit Listener(rex::IPhysicalMemory& owner) : context(owner) {}
      rex::PhysicalRange OnPhysicalWrite(rex::PhysicalRange range, bool) override {
        auto value = context.LoadAcquire32(address);
        REQUIRE(value);
        observed.push_back(*value);
        return range;
      }
    } listener(context);
    auto registration = watches.AddWriteListener(listener);

    watches.WatchPhysicalWrites({address, 4});
    REQUIRE(context.StoreRelease32(address, 0xCAFE1234));
    REQUIRE_FALSE(listener.observed.empty());
    for (const auto value : listener.observed) CHECK(value == 0xCAFE1234);

    listener.observed.clear();
    watches.WatchPhysicalWrites({address, 4});
    const std::array<std::byte, 4> bytes = {
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    REQUIRE(context.CopyToPhysical(address, bytes));
    REQUIRE_FALSE(listener.observed.empty());
    for (const auto value : listener.observed) CHECK(value == 0xDEADBEEF);

    listener.observed.clear();
    watches.WatchPhysicalWrites({address, 4});
    auto writable = context.GetWritablePhysicalSpan({address, 4});
    REQUIRE(writable);
    std::fill(writable->begin(), writable->end(), std::byte{0x11});
    CHECK(listener.observed.empty());
    REQUIRE(context.NotifyPhysicalWrite({address, 4}));
    REQUIRE_FALSE(listener.observed.empty());
    for (const auto value : listener.observed) CHECK(value == 0x11111111);

    const auto count = listener.observed.size();
    REQUIRE(context.NotifyPhysicalWrite({0x20000000, 0}));
    CHECK_FALSE(context.NotifyPhysicalWrite({0x1FFFFFFF, 2}));
    CHECK_FALSE(context.NotifyPhysicalWrite({0xFFFFFFFF, 1}));
    CHECK(listener.observed.size() == count);
    const auto final_value = context.LoadAcquire32(address);
    REQUIRE(final_value);
    CHECK(*final_value == 0x11111111);
  });
}
