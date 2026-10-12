#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "../../titles/dash_17559/src/experimental_region.h"

TEST_CASE("dashboard experimental region is opt-out zeroed once and rejects overlaps", "[sdk][memory]") {
  using namespace rex::memory;
  using Region = dash17559::ExperimentalRegion;
  Memory memory;
  REQUIRE(memory.Initialize());
  auto* heap = memory.LookupHeap(Region::kBase);
  REQUIRE(heap);
  Region region;
  HeapAllocationInfo info{};
  CHECK(region.Map(memory, false) == Region::Result::Disabled);
  REQUIRE(heap->QueryRegionInfo(Region::kBase, &info));
  CHECK(info.state == 0);

  SECTION("enabled region has exact bounds and preserves guest writes") {
    REQUIRE(region.Map(memory, true) == Region::Result::Mapped);
    REQUIRE(heap->QueryRegionInfo(Region::kBase, &info));
    CHECK(info.allocation_base == Region::kBase);
    CHECK(info.allocation_size == Region::kSize);
    CHECK(info.protect == (kMemoryProtectRead | kMemoryProtectWrite));
    CHECK(info.state == (kMemoryAllocationReserve | kMemoryAllocationCommit));
    auto* bytes = memory.TranslateVirtual(Region::kBase);
    CHECK(std::all_of(bytes, bytes + Region::kSize, [](uint8_t b) { return b == 0; }));
    bytes[0x8630] = 0x40;
    bytes[Region::kSize - 1] = 0xA5;
    CHECK(region.Map(memory, true) == Region::Result::AlreadyMapped);
    CHECK(bytes[0x8630] == 0x40);
    CHECK(bytes[Region::kSize - 1] == 0xA5);
    Region other;
    CHECK(other.Map(memory, true) == Region::Result::Overlap);
    CHECK(bytes[0x8630] == 0x40);
    REQUIRE(heap->QueryRegionInfo(Region::kBase + Region::kSize, &info));
    CHECK(info.state == 0);
  }
  SECTION("reserved pages are not adopted") {
    REQUIRE(heap->AllocFixed(Region::kBase, Region::kSize, Region::kSize,
                            kMemoryAllocationReserve, kMemoryProtectRead));
    CHECK(region.Map(memory, true) == Region::Result::Overlap);
    REQUIRE(heap->QueryRegionInfo(Region::kBase, &info));
    CHECK(info.state == kMemoryAllocationReserve);
    CHECK(info.protect == kMemoryProtectRead);
  }
}
