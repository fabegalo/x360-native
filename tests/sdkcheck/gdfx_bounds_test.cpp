#include "filesystem/container_parse.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <vector>

namespace {
constexpr uint32_t kSector = 2048, kTable = 33 * kSector;
void Put16(std::vector<uint8_t> &bytes, size_t at, uint16_t value) {
  bytes.at(at) = uint8_t(value);
  bytes.at(at + 1) = uint8_t(value >> 8);
}
void Put32(std::vector<uint8_t> &bytes, size_t at, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.at(at + i) = uint8_t(value >> (8 * i));
}
void Entry(std::vector<uint8_t> &bytes, size_t at, uint32_t sector,
           uint32_t size, bool directory = false) {
  Put32(bytes, at + 4, sector);
  Put32(bytes, at + 8, size);
  bytes.at(at + 12) = directory ? 0x10 : 0x80;
  bytes.at(at + 13) = 1;
  bytes.at(at + 14) = 'a';
}
std::vector<uint8_t> Image() {
  std::vector<uint8_t> bytes(40 * kSector);
  std::memcpy(bytes.data() + 32 * kSector, "MICROSOFT*XBOX*MEDIA", 20);
  Put32(bytes, 32 * kSector + 20, 33);
  Put32(bytes, 32 * kSector + 24, 32);
  Entry(bytes, kTable, 34, 3);
  return bytes;
}
} // namespace

TEST_CASE("GDFX rejects records escaping their declared table and overlapping "
          "aliases",
          "[sdk][gdfx]") {
  for (unsigned variant = 0; variant < 12; ++variant) {
    CAPTURE(variant);
    auto bytes = Image();
    switch (variant) {
    case 0:
      break;
    case 1:
      Put32(bytes, 32 * kSector + 24, 14);
      break; // name outside table
    case 2:
      Put16(bytes, kTable + 2, 8);
      Entry(bytes, kTable + 32, 34, 3);
      break;
    case 3:
      Put16(bytes, kTable + 2, 1);
      break; // overlapping record
    case 4:
      Entry(bytes, kTable, 33, 32, true);
      break; // directory cycle
    case 5:
      Entry(bytes, kTable, 0xFFFFFFFF, 3);
      break;
    case 6:
      Entry(bytes, kTable, 39, kSector + 1);
      break;
    case 7:
      Entry(bytes, kTable, 34, 13, true);
      break;
    case 8:
      bytes[kTable + 13] = 0;
      break;
    case 9:
      bytes[kTable + 14] = 0;
      break;
    case 10:
      Put32(bytes, 32 * kSector + 24, 13);
      break;
    case 11: // Two sibling pointers referring to the same nonempty record.
      Put16(bytes, kTable, 4);
      Put16(bytes, kTable + 2, 4);
      Entry(bytes, kTable + 16, 34, 3);
      break;
    }
    std::vector<rex::filesystem::ContainerNode> nodes(1);
    const auto result = rex::filesystem::detail::ParseGDFX(bytes, nodes);
    CHECK(result.has_value() == (variant == 0));
    if (result) {
      REQUIRE(nodes.size() == 2);
      CHECK(nodes[1].extents.front().offset == 34 * kSector);
      CHECK(nodes[1].info.size == 3);
    }
  }
}

TEST_CASE("GDFX bounds directory depth without recursive traversal",
          "[sdk][gdfx]") {
  for (unsigned depth : {256u, 257u}) {
    CAPTURE(depth);
    auto bytes = Image();
    bytes.resize((34 + depth) * kSector);
    for (unsigned i = 0; i < depth; ++i)
      Entry(bytes, kTable + i * kSector, 34 + i, 32, true);
    Entry(bytes, kTable + depth * kSector, 0, 0);
    std::vector<rex::filesystem::ContainerNode> nodes(1);
    CHECK(rex::filesystem::detail::ParseGDFX(bytes, nodes).has_value() ==
          (depth == 256));
  }
}
