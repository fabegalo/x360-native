#include "../../src/native/commands/occlusion_query.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <rex/memory/ring_buffer.h>
#include <rex/graphics/xenos.h>
namespace rex::graphics {
void ValidatePacketForBackend(uint32_t, const rex::memory::RingBuffer&);
}
using namespace x360n::native;
TEST_CASE("Native visibility queries accumulate independently and reset on BEGIN", "[native-zpd]") {
  VisibilityQueries queries;
  for (unsigned id = 0; id < 64; ++id) queries.Begin(id);
  for (unsigned id = 0; id < 64; ++id) {
    queries.Accumulate(id, (id & 1) != 0);
    queries.Accumulate(id, false); // later occluded draws do not clear visibility
  }
  for (unsigned id = 0; id < 64; ++id) {
    CHECK(queries.End(id) == ((id & 1) != 0));
    CHECK_THROWS(queries.End(id));
    queries.Begin(id);
    CHECK_FALSE(queries.End(id));
  }
}
TEST_CASE("Native draw predication selects all 64 visibility IDs", "[native-zpd]") {
  for (unsigned id = 0; id < 64; ++id) {
    CHECK(VisibilityDrawEnabled(id, 0, 0));
    CHECK_FALSE(VisibilityDrawEnabled(id | 0x100, 0, 0));
    const uint32_t low = id < 32 ? 1u << id : 0;
    const uint32_t high = id >= 32 ? 1u << (id - 32) : 0;
    CHECK(VisibilityDrawEnabled(id | 0x100, low, high));
    CHECK_FALSE(VisibilityDrawEnabled(((id + 1) & 63) | 0x100, low, high));
  }
}
TEST_CASE("Native ZPD uses the D3D pending token and preserves overlapping query intervals", "[native-zpd]") {
  SampleCounts report{};
  CHECK_FALSE(OcclusionQueries::IsEnd(report));
  for (unsigned i : {2u, 3u, 4u, 5u}) {
    report = {}; report[i] = 0xEDFEFFFF;
    CHECK(OcclusionQueries::IsEnd(report));
  }
  report = {}; report[4] = 0xFFFFFEED;
  CHECK_FALSE(OcclusionQueries::IsEnd(report));
  report = {}; report[0] = 0xEDFEFFFF;
  CHECK_FALSE(OcclusionQueries::IsEnd(report));
  OcclusionQueries queries;
  CHECK_THROWS_WITH(queries.CountingAfter(true), "Native ZPD: END without a matching BEGIN");
  REQUIRE(queries.CountingAfter(false)); queries.Commit(false);
  REQUIRE(queries.CountingAfter(false)); queries.Commit(false);
  REQUIRE(queries.CountingAfter(true)); queries.Commit(true);
  REQUIRE_FALSE(queries.CountingAfter(true)); queries.Commit(true);
  CHECK(queries.active() == 0);
  // An unsuccessful backend read must not advance the query interval state.
  CHECK(queries.CountingAfter(false));
  CHECK(queries.active() == 0);
}
TEST_CASE("Native validates ZPD packets before execution", "[native-zpd]") {
  using namespace rex::graphics;
  uint32_t payload[]{rex::byte_swap(uint32_t(xenos::ZPASS_DONE)), 0, 0};
  rex::memory::RingBuffer ring(reinterpret_cast<uint8_t*>(payload), sizeof(payload));
  ring.set_write_offset(4);
  const uint32_t header = 0xC0000000 | (uint32_t(xenos::PM4_EVENT_WRITE_ZPD) << 8);
  CHECK_NOTHROW(ValidatePacketForBackend(header, ring));
  CHECK(ring.read_offset() == 0);
  CHECK_THROWS_WITH(ValidatePacketForBackend(header | (1u << 16), ring), "truncated PM4 packet");
  ring.set_write_offset(8);
  CHECK_THROWS_WITH(ValidatePacketForBackend(header | (1u << 16), ring), "invalid PM4 payload size");
}
