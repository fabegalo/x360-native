// x360n::avatar asset IDs, body type and result sizes (no game or system content).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../../titles/common/avatar/avatar_metadata.h"

using namespace x360n::avatar;

namespace {

void Put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[o + i] = uint8_t(v >> (24 - 8 * i));
}

// Catalog with two used records and one unused slot, current format, 18 columns;
// record +0x04 holds the compatibility byte.
std::vector<uint8_t> Catalog() {
  const std::array<uint8_t, 16> guid = {0x58, 0x0a, 0x07, 0xd6, 0x4b, 0xcd, 0x40, 0xbe,
                                        0xbf, 0x37, 0x25, 0x4b, 0xe8, 0x26, 0xfb, 0x0b};
  const uint32_t rs = 0x10C, n = 3, table_end = 48 + n * rs;
  std::vector<uint8_t> b(table_end, 0);
  std::copy(guid.begin(), guid.end(), b.begin());
  Put32(b, 0x10, 0x01c8f109);
  Put32(b, 0x14, 0xa19cb2e0);
  Put32(b, 0x1C, n);
  Put32(b, 0x28, 18);
  const uint32_t str = table_end, blobs = table_end + 4;
  const uint32_t category[3] = {2, 0x100000, 0};
  const uint8_t compat[3] = {1, 3, 0};
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t r = 48 + i * rs;
    Put32(b, r, category[i]);
    b[r + 4] = compat[i];
    if (i == 2) continue;  // unused slot
    for (uint32_t c = 0; c < 18; ++c) Put32(b, r + 0xBC + 4 * c, str);
    Put32(b, r + 0xBC + 72, blobs + 4 * i);
    Put32(b, r + 0xBC + 76, 4);
  }
  b.insert(b.end(), {0, 'A', 0, 0});
  b.insert(b.end(), {'S', 'T', 'R', 'B', 'S', 'T', 'R', 'B'});
  return b;
}

AssetId Id(uint32_t category, uint16_t index, uint8_t compat) {
  return {category, index, 0, compat, 0xC1C8F109A19CB2E0ull};
}

}  // namespace

TEST_CASE("avatar asset IDs round-trip and resolve to catalog records", "[avatar]") {
  std::string error;
  auto toc = AvatarToc::Parse(Catalog(), &error);
  REQUIRE(toc);
  CHECK(AssetStampFor(*toc) == 0xC1C8F109A19CB2E0ull);

  const AssetId body = Id(2, 0, 1);
  const auto bytes = body.ToBytes();
  CHECK(bytes == std::array<uint8_t, 16>{0, 0, 0, 2, 0, 0, 0, 1, 0xc1, 0xc8, 0xf1, 0x09, 0xa1, 0x9c,
                                         0xb2, 0xe0});
  const AssetId back = AssetId::FromBytes(bytes);
  CHECK(back.category == 2);
  CHECK(back.index == 0);
  CHECK(back.compatibility == 1);
  CHECK(back.stamp == body.stamp);

  CHECK(ResolveAsset(*toc, body, &error) == 0u);
  CHECK(ResolveAsset(*toc, Id(0x100000, 1, 3), &error) == 1u);

  CHECK_FALSE(ResolveAsset(*toc, Id(0x100000, 0, 1), &error));
  CHECK(error == "asset ID category does not match the record");
  CHECK_FALSE(ResolveAsset(*toc, Id(2, 0, 2), &error));
  CHECK(error == "asset ID compatibility byte does not match the record");
  CHECK_FALSE(ResolveAsset(*toc, Id(0, 2, 0), &error));
  CHECK(error == "asset ID names an unused catalog slot");
  CHECK_FALSE(ResolveAsset(*toc, Id(2, 3, 1), &error));
  CHECK(error == "asset ID index beyond the catalog");
  AssetId other = body;
  other.stamp = 0x01C8F109A19CB2E0ull;  // header stamp without the 0xC nibble
  CHECK_FALSE(ResolveAsset(*toc, other, &error));
  CHECK(error == "asset ID stamp does not match the catalog");
}

TEST_CASE("avatar body type compares the body ID at +0x120 with XAM's constants", "[avatar]") {
  std::array<uint8_t, kMetadataSize> m{};
  CHECK(BodyType(m) == 0);
  auto put = [&](const AssetId& id) {
    const auto b = id.ToBytes();
    std::copy(b.begin(), b.end(), m.begin() + kBodyAssetAt);
  };
  put(Id(2, 0, 1));
  CHECK(BodyType(m) == 1);
  put(Id(2, 1, 2));
  CHECK(BodyType(m) == 2);
  put(Id(2, 1, 1));  // FemaleBody index with the male compatibility byte: no match
  CHECK(BodyType(m) == 0);
}

TEST_CASE("avatar result sizes follow XAM's component table", "[avatar]") {
  CHECK(ResultSizeFor(0).result == 0x1F80);
  CHECK(ResultSizeFor(0).gpu_resources == 0);
  CHECK(ResultSizeFor(0x0001).result == 0x1F80 + 0x800);
  CHECK(ResultSizeFor(0x0001).gpu_resources == 0x54000);  // 0x53800 rounded to 4 KB
  CHECK(ResultSizeFor(0x0002).result == 0x1F80 + 0x400);
  CHECK(ResultSizeFor(0x1000).result == 0x3147C + 0x800);
  CHECK(ResultSizeFor(0x1000).gpu_resources == 0x19000);
  const auto all = ResultSizeFor(0x1FFF);
  CHECK(all.result == 0x3147C + 12 * 0x800 + 0x400);
  CHECK(all.gpu_resources == 0x54000 + 0x13000 + 0xE000 + 0x15000 + 0xF000 + 0xE000 + 0xB000 +
                                 0x7000 + 0x9000 + 0x6000 + 0x6000 + 0x6000 + 0x19000);
  CHECK(ResultSizeFor(0xE000).result == 0x1F80);  // bits above the 13 components ignored
}
