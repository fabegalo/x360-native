#include <catch2/catch_test_macros.hpp>
#include <rex/graphics/memexport_readback.h>
#include <array>
#include <limits>

using namespace rex::graphics;
using Range = draw_util::MemExportRange;

TEST_CASE("MEMEXPORT snapshot merge preserves unwritten 8 and 16 bit neighbors", "[memexport]") {
  const std::array<uint8_t, 9> before{1, 2, 3, 4, 5, 6, 7, 8, 9};
  const std::array<uint8_t, 9> after{1, 42, 3, 4, 64, 65, 7, 8, 9};
  std::array<uint8_t, 9> guest{91, 92, 93, 94, 95, 96, 97, 98, 99};
  PublishMemexportChanges(guest.data(), before.data(), after.data(), uint32_t(guest.size()));
  const std::array<uint8_t, 9> expected{91, 42, 93, 94, 64, 65, 97, 98, 99};
  REQUIRE(guest == expected);
}

TEST_CASE("MEMEXPORT delayed slots require identical complete range layouts", "[memexport]") {
  const std::vector<Range> first{Range{0x100, 16}, Range{0x200, 16}};
  const std::vector<Range> different_second{Range{0x100, 16}, Range{0x300, 16}};
  const std::vector<Range> different_lengths{Range{0x100, 8}, Range{0x200, 24}};
  REQUIRE(SameMemexportReadbackRanges(first, first));
  REQUIRE_FALSE(SameMemexportReadbackRanges(first, different_second));
  REQUIRE_FALSE(SameMemexportReadbackRanges(first, different_lengths));
  REQUIRE_FALSE(SameMemexportReadbackRanges(first, {Range{0x100, 32}}));
}

TEST_CASE("MEMEXPORT readback allocation is exact and bounded", "[memexport]") {
  uint32_t size = 0;
  REQUIRE(GetMemexportReadbackSize({Range{0x100, 1}, Range{0x200, 2}, Range{0x300, 5}}, size));
  REQUIRE(size == 8);
  REQUIRE(GetMemexportReadbackSize({Range{(1u << 27) - 1, 4}}, size));
  REQUIRE(size == 4);
  REQUIRE_FALSE(GetMemexportReadbackSize({Range{(1u << 27) - 1, 5}}, size));
  REQUIRE_FALSE(GetMemexportReadbackSize({Range{1u << 27, 4}}, size));
  REQUIRE_FALSE(GetMemexportReadbackSize({Range{0x100, UINT32_MAX}}, size));
  REQUIRE_FALSE(GetMemexportReadbackSize({Range{0, 1u << 29}, Range{0, 1u << 29},
                                        Range{0, 1u << 29}, Range{0, 1u << 29}}, size));
}
