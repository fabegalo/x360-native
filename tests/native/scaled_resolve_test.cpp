#include "../../src/native/memory/scaled_resolve.h"
#include "../../src/native/memory/physical_write.h"
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace x360n::native;

TEST_CASE("Native scaled resolve mapping: the replication inverse matches the pinned layout", "[native-memory]") {
  for (auto [sx, sy] : {std::pair{2u, 2u}, std::pair{3u, 3u}, std::pair{3u, 2u}, std::pair{1u, 2u}})
    for (unsigned pixel_log2 = 0; pixel_log2 <= 4; ++pixel_log2) {
      const unsigned area = sx * sy;
      const uint32_t bytes = 512;
      // Every scaled byte is the subpixel of exactly one guest byte.
      std::vector<unsigned> hits(size_t(bytes) * area);
      for (uint32_t address = 0; address < bytes; ++address)
        for (unsigned s = 0; s < area; ++s) {
          const auto offset = ScaledResolveByteOffset(address, pixel_log2, sx, sy, s % sx, s / sx);
          REQUIRE(offset < hits.size());
          ++hits[offset];
          REQUIRE(ScaledResolveGuestByte(offset, pixel_log2, sx, sy) == address);
        }
      for (auto count : hits) REQUIRE(count == 1);
    }
}

TEST_CASE("Native scaled resolve pages: stale runs, current pages and retirement", "[native-memory]") {
  rex::memory::Memory memory;
  REQUIRE(memory.Initialize());
  const uint32_t allocated = memory.SystemHeapAlloc(4 * 4096, 4096, rex::memory::kSystemHeapPhysical);
  REQUIRE(allocated);
  const uint32_t base = memory.GetPhysicalAddress(allocated);
  GuestMemoryMirror mirror(memory, [](uint32_t, uint32_t) {});
  ScaledResolveCache cache(2, 2);
  cache.Attach(mirror);
  using Runs = std::vector<std::pair<uint32_t, uint32_t>>;
  // Nothing written at scale: one page-aligned run, no scaled data.
  CHECK(cache.Stale(base + 100, 2 * 4096) == Runs{{base, 3 * 4096}});
  CHECK_FALSE(cache.Overlaps(base, 4 * 4096));
  // A resolve covering page 1 (partially) makes the whole page current.
  mirror.RequestRange(base, 4 * 4096);
  mirror.RangeWrittenByGpu(base + 4096 + 256, 512);
  cache.MarkCurrent(base + 4096 + 256, 512);
  CHECK(cache.Overlaps(base + 4096, 1));
  CHECK_FALSE(cache.Overlaps(base, 4096));
  CHECK(cache.Stale(base, 4 * 4096) == Runs{{base, 4096}, {base + 2 * 4096, 2 * 4096}});
  // A CPU write to the page retires its subpixels.
  mirror.RequestRange(base + 4096, 4096);
  *memory.TranslatePhysical<uint32_t*>(base + 4096) = 0x12345678;
  NotifyPhysicalWrite(memory, base + 4096, 4);
  CHECK_FALSE(cache.Overlaps(base + 4096, 4096));
  CHECK(cache.Stale(base + 4096, 4096) == Runs{{base + 4096, 4096}});
  // Current again, then a non-scaled GPU write (memexport) retires it too.
  cache.MarkCurrent(base + 4096, 4096);
  CHECK(cache.Stale(base + 4096, 4096).empty());
  mirror.RangeWrittenByGpu(base + 4096 + 8, 4);
  CHECK(cache.Stale(base + 4096, 4096) == Runs{{base + 4096, 4096}});
  cache.Clear();
}
