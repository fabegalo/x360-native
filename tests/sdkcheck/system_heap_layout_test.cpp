#include <catch2/catch_test_macros.hpp>
#include <rex/system/xmemory.h>
#include <algorithm>

using namespace rex::memory;

TEST_CASE("System objects share the virtual allocator and its reusable holes",
          "[sdk][system-heap-layout]") {
  Memory memory;
  REQUIRE(memory.Initialize());
  auto* heap = memory.LookupHeapByType(false, 4096);
  const auto free_before = heap->GetUnreservedPageCount();
  uint32_t zero_before = ~0u;
  REQUIRE(heap->QueryProtect(0, &zero_before));
  constexpr uint32_t allocation = kMemoryAllocationReserve | kMemoryAllocationCommit;
  constexpr uint32_t protect = kMemoryProtectRead | kMemoryProtectWrite;
  uint32_t first = 0;
  REQUIRE(heap->Alloc(8192, 4096, allocation, protect, false, &first));
  REQUIRE(heap->Release(first));

  const auto system = memory.SystemHeapAlloc(8192, 4096);
  REQUIRE(system);
  CHECK(system == first);
  HeapAllocationInfo info{};
  REQUIRE(heap->QueryRegionInfo(system, &info));
  CHECK(info.allocation_size == 8192);
  CHECK((info.state & allocation) == allocation);
  CHECK((info.protect & protect) == protect);
  CHECK(heap->GetUnreservedPageCount() == free_before - 2);

  uint32_t guest = 0;
  REQUIRE(heap->Alloc(4096, 4096, allocation, protect, false, &guest));
  CHECK(guest == first + 8192);
  std::fill_n(memory.TranslateVirtual(system), 8192, uint8_t{0xA5});
  memory.SystemHeapFree(system);
  REQUIRE(heap->QueryRegionInfo(system, &info));
  CHECK(info.state == 0);

  const auto reused = memory.SystemHeapAlloc(8192, 4096);
  REQUIRE(reused);
  CHECK(reused == first);
  CHECK(std::all_of(memory.TranslateVirtual(reused), memory.TranslateVirtual(reused) + 8192,
                    [](uint8_t value) { return value == 0; }));
  memory.SystemHeapFree(reused);
  CHECK(heap->Release(guest));
  CHECK(heap->GetUnreservedPageCount() == free_before);
  REQUIRE(heap->QueryRegionInfo(first, &info));
  CHECK(info.state == 0);  // No synthetic allocation remains after free.
  uint32_t zero_protect = ~0u;
  REQUIRE(heap->QueryProtect(0, &zero_protect));
  CHECK(zero_protect == zero_before);
}

TEST_CASE("Physical system objects preserve physical allocation and zeroing",
          "[sdk][system-heap-layout]") {
  Memory memory;
  REQUIRE(memory.Initialize());
  auto* heap = memory.LookupHeapByType(true, 4096);
  const auto free_before = heap->GetUnreservedPageCount();
  uint32_t expected = 0;
  REQUIRE(heap->Alloc(4096, 4096, kMemoryAllocationReserve | kMemoryAllocationCommit,
                       kMemoryProtectRead | kMemoryProtectWrite, false, &expected));
  REQUIRE(heap->Release(expected));
  const auto address = memory.SystemHeapAlloc(4096, 4096, kSystemHeapPhysical);
  REQUIRE(address);
  CHECK(address == expected);
  CHECK(memory.GetPhysicalAddress(address) != UINT32_MAX);
  CHECK(std::all_of(memory.TranslateVirtual(address), memory.TranslateVirtual(address) + 4096,
                    [](uint8_t value) { return value == 0; }));
  memory.SystemHeapFree(address);
  CHECK(heap->GetUnreservedPageCount() == free_before);
}

TEST_CASE("System objects retain their reserve when the ordinary virtual range is full",
          "[sdk][system-heap-layout]") {
  Memory memory;
  REQUIRE(memory.Initialize());
  auto* heap = memory.LookupHeapByType(false, 4096);
  const auto free_before = heap->GetUnreservedPageCount();
  // The ordinary 4 KiB allocator excludes the upper 256 MiB. Preserve that
  // existing reserve for kernel objects instead of making them compete for
  // the last ordinary page. Reservation alone needs no large committed buffer.
  constexpr uint32_t ordinary_size = 0x30000000u - 0x10000u;
  uint32_t ordinary = 0;
  REQUIRE(heap->Alloc(ordinary_size, 4096, kMemoryAllocationReserve,
                       kMemoryProtectNoAccess, false, &ordinary));
  const auto system = memory.SystemHeapAlloc(8192, 4096);
  REQUIRE(system);
  CHECK(system >= ordinary + ordinary_size);
  HeapAllocationInfo info{};
  REQUIRE(heap->QueryRegionInfo(system, &info));
  CHECK((info.state & kMemoryAllocationCommit) != 0);
  memory.SystemHeapFree(system);
  CHECK(heap->Release(ordinary));
  CHECK(heap->GetUnreservedPageCount() == free_before);
}

TEST_CASE("Ordinary top-down allocations end at the inclusive range limit",
          "[sdk][system-heap-layout]") {
  // The ordinary limit is inclusive (0x2FFFFFFF, 0x6FFFFFFF). Rounding it up
  // admitted the first page above it: 17559 bootanim reserved 0x300000 bytes
  // at 2FD01000-30001000 and its 64 KiB heap arithmetic wrote at 30001002.
  Memory memory;
  REQUIRE(memory.Initialize());
  constexpr uint32_t allocation = kMemoryAllocationReserve | kMemoryAllocationCommit;
  constexpr uint32_t protect = kMemoryProtectRead | kMemoryProtectWrite;
  struct Case {
    uint32_t page_size, size, limit;
  };
  for (const auto& c : {Case{4096, 0x300000, 0x30000000}, Case{0x10000, 0x10000, 0x70000000}}) {
    auto* heap = memory.LookupHeapByType(false, c.page_size);
    const auto free_before = heap->GetUnreservedPageCount();
    uint32_t address = 0;
    REQUIRE(heap->Alloc(c.size, c.page_size, allocation, protect, true, &address));
    CHECK(address == c.limit - c.size);
    CHECK(address % 0x10000 == 0);
    HeapAllocationInfo above{};
    REQUIRE(heap->QueryRegionInfo(c.limit, &above));
    CHECK(above.state == 0);
    CHECK(heap->Release(address));
    CHECK(heap->GetUnreservedPageCount() == free_before);
  }
}
