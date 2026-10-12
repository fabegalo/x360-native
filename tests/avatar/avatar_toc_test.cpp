// x360n::avatar::AvatarToc over synthetic catalogs (no game or system content).
// Layout as described in titles/common/avatar/avatar_toc.h.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../../titles/common/avatar/avatar_toc.h"

using namespace x360n::avatar;

namespace {

const std::array<uint8_t, 16> kCurrent = {0x58, 0x0a, 0x07, 0xd6, 0x4b, 0xcd, 0x40, 0xbe,
                                          0xbf, 0x37, 0x25, 0x4b, 0xe8, 0x26, 0xfb, 0x0b};
const std::array<uint8_t, 16> kLegacy = {0x8a, 0x76, 0x2d, 0xf4, 0xc0, 0x67, 0x4b, 0x66,
                                         0xb4, 0xee, 0x56, 0x46, 0x6b, 0x1b, 0x82, 0x80};
const std::array<uint8_t, 16> kOldest = {0x9a, 0xd6, 0xeb, 0xce, 0x62, 0x62, 0x4e, 0xeb,
                                         0x8a, 0x82, 0xa3, 0xf7, 0x0b, 0x81, 0x73, 0x69};

void Put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  b[o] = uint8_t(v >> 24);
  b[o + 1] = uint8_t(v >> 16);
  b[o + 2] = uint8_t(v >> 8);
  b[o + 3] = uint8_t(v);
}

struct Rec {
  uint32_t field_00;
  std::u16string name;  // same string in every column
  std::vector<uint8_t> blob;
};

// header_size 48/40/32; columns written at +0x28 only for the 48-byte header.
std::vector<uint8_t> Make(const std::array<uint8_t, 16>& guid, uint32_t header_size,
                          uint32_t columns, const std::vector<Rec>& recs) {
  const uint32_t rs = 0xBC + 4 * columns + 8;
  const uint32_t table_end = header_size + uint32_t(recs.size()) * rs;
  std::vector<uint8_t> strings;
  std::vector<uint32_t> name_at;
  for (const auto& r : recs) {
    name_at.push_back(table_end + uint32_t(strings.size()));
    for (char16_t c : r.name) {
      strings.push_back(uint8_t(c >> 8));
      strings.push_back(uint8_t(c));
    }
    strings.push_back(0);
    strings.push_back(0);
  }
  const uint32_t blobs_begin = table_end + uint32_t(strings.size());
  std::vector<uint8_t> b(table_end, 0);
  std::copy(guid.begin(), guid.end(), b.begin());
  Put32(b, 0x18, 2);
  Put32(b, 0x1C, uint32_t(recs.size()));
  if (header_size > 0x20) Put32(b, 0x20, 2);
  if (header_size > 0x28) Put32(b, 0x28, columns);
  uint32_t blob_at = blobs_begin;
  for (size_t i = 0; i < recs.size(); ++i) {
    const size_t r = header_size + i * rs;
    Put32(b, r, recs[i].field_00);
    for (uint32_t c = 0; c < columns; ++c) Put32(b, r + 0xBC + 4 * c, name_at[i]);
    Put32(b, r + 0xBC + 4 * columns, blob_at);
    Put32(b, r + 0xBC + 4 * columns + 4, uint32_t(recs[i].blob.size()));
    blob_at += uint32_t(recs[i].blob.size());
  }
  b.insert(b.end(), strings.begin(), strings.end());
  for (const auto& r : recs) b.insert(b.end(), r.blob.begin(), r.blob.end());
  return b;
}

const std::vector<Rec> kRecs = {{2, u"Shirt", {'S', 'T', 'R', 'B', 1}}, {0x400, u"Hat", {}},
                                {8, u"Shoes", {'S', 'T', 'R', 'B', 2, 3}}};

}  // namespace

TEST_CASE("avatar catalog: current format with 18 columns", "[avatar]") {
  std::string error;
  auto toc = AvatarToc::Parse(Make(kCurrent, 48, 18, kRecs), &error);
  REQUIRE(toc);
  CHECK(toc->header().format == TocFormat::kCurrent);
  CHECK(toc->header().record_size == 0x10C);
  CHECK(toc->header().column_count == 18);
  REQUIRE(toc->size() == 3);
  CHECK(toc->record(1).field_00 == 0x400);
  CHECK(toc->Name(0, 1) == u"Shirt");
  CHECK(toc->Name(2, 17) == u"Shoes");
  CHECK(toc->Blob(1).empty());
  REQUIRE(toc->Blob(2).size() == 6);
  CHECK(toc->Blob(2)[5] == 3);
  CHECK(toc->Opaque(0).size() == 0xB8);
}

TEST_CASE("avatar catalog: short headers use XAM's defaults", "[avatar]") {
  std::string error;
  auto legacy = AvatarToc::Parse(Make(kLegacy, 40, 13, kRecs), &error);
  REQUIRE(legacy);
  CHECK(legacy->header().format == TocFormat::kLegacyV1);
  CHECK(legacy->header().column_count == 13);
  CHECK(legacy->header().record_size == 0xF8);
  CHECK(legacy->header().field_20 == 2);
  auto oldest = AvatarToc::Parse(Make(kOldest, 32, 13, kRecs), &error);
  REQUIRE(oldest);
  CHECK(oldest->header().format == TocFormat::kOldest);
  CHECK(oldest->header().field_20 == 1);
  CHECK(oldest->header().column_count == 13);
  CHECK(oldest->Name(2, 12) == u"Shoes");
}

TEST_CASE("avatar catalog: unused slots have no strings and no blob", "[avatar]") {
  std::string error;
  auto data = Make(kCurrent, 48, 18, kRecs);
  const size_t r1 = 48 + 0x10C;  // record 1 ("Hat", empty blob)
  for (uint32_t c = 0; c < 18; ++c) Put32(data, r1 + 0xBC + 4 * c, 0);
  Put32(data, r1 + 0xBC + 72, 0);
  auto toc = AvatarToc::Parse(data, &error);
  INFO(error);
  REQUIRE(toc);
  CHECK(toc->record(1).empty);
  CHECK_FALSE(toc->record(0).empty);
  CHECK(toc->Name(1, 1).empty());
  CHECK(toc->Blob(1).empty());

  auto half = Make(kCurrent, 48, 18, kRecs);
  Put32(half, r1 + 0xBC + 72, 0);  // blob offset 0 but names still present
  CHECK_FALSE(AvatarToc::Parse(half, &error));
  CHECK(error == "record 1 is neither used nor an empty slot");
}

TEST_CASE("avatar catalog: rejects malformed data", "[avatar]") {
  std::string error;
  auto good = Make(kCurrent, 48, 18, kRecs);

  auto bad_guid = good;
  bad_guid[0] ^= 0xFF;
  CHECK_FALSE(AvatarToc::Parse(bad_guid, &error));
  CHECK(error == "unknown catalog GUID");

  auto too_many = good;
  Put32(too_many, 0x1C, 0x10000);
  CHECK_FALSE(AvatarToc::Parse(too_many, &error));
  CHECK(error == "record table does not fit in the catalog");

  auto truncated = good;
  truncated.pop_back();
  CHECK_FALSE(AvatarToc::Parse(truncated, &error));
  CHECK(error.find("exceeds") != std::string::npos);

  auto gap = good;
  gap.push_back(0);
  CHECK_FALSE(AvatarToc::Parse(gap, &error));
  CHECK(error == "last blob does not end at the end of the catalog");

  auto bad_string = good;
  Put32(bad_string, 48 + 0xBC, 4);  // record 0, column 0 -> inside the header
  CHECK_FALSE(AvatarToc::Parse(bad_string, &error));
  CHECK(error.find("outside the string area") != std::string::npos);

  auto columns = good;
  Put32(columns, 0x28, 0);
  CHECK_FALSE(AvatarToc::Parse(columns, &error));
  CHECK(error == "column count out of range");

  CHECK_FALSE(AvatarToc::Parse(std::vector<uint8_t>(16, 0), &error));
}
