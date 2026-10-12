#include "avatar_metadata.h"

#include <algorithm>

namespace x360n::avatar {
namespace {

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// XAM 2.0.17559.0, 0x81632E68 and 0x81632E78 (the MaleBody and FemaleBody asset IDs).
constexpr std::array<uint8_t, 16> kBodyType1 = {0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
                                                0xc1, 0xc8, 0xf1, 0x09, 0xa1, 0x9c, 0xb2, 0xe0};
constexpr std::array<uint8_t, 16> kBodyType2 = {0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x02,
                                                0xc1, 0xc8, 0xf1, 0x09, 0xa1, 0x9c, 0xb2, 0xe0};

// XAM 2.0.17559.0, 0x816332B8: 13 x {u32 index, u16 mask, u16 0, u32 result bytes,
// u32 GPU bytes}; the mask of entry i is 1 << i.
struct ComponentSize {
  uint32_t result;
  uint32_t gpu;
};
constexpr std::array<ComponentSize, 13> kComponentSizes = {{
    {0x800, 0x53800}, {0x400, 0x12C00}, {0x800, 0xE000},  {0x800, 0x14400}, {0x800, 0xF000},
    {0x800, 0xD400},  {0x800, 0xA400},  {0x800, 0x6800},  {0x800, 0x8400},  {0x800, 0x5C00},
    {0x800, 0x5800},  {0x800, 0x5C00},  {0x800, 0x19000},
}};

}  // namespace

AssetId AssetId::FromBytes(std::span<const uint8_t, 16> b) {
  AssetId id;
  id.category = Be32(b.data());
  id.index = uint16_t((b[4] << 8) | b[5]);
  id.reserved = b[6];
  id.compatibility = b[7];
  id.stamp = (uint64_t(Be32(b.data() + 8)) << 32) | Be32(b.data() + 12);
  return id;
}

std::array<uint8_t, 16> AssetId::ToBytes() const {
  std::array<uint8_t, 16> b{};
  for (int i = 0; i < 4; ++i) b[i] = uint8_t(category >> (24 - 8 * i));
  b[4] = uint8_t(index >> 8);
  b[5] = uint8_t(index);
  b[6] = reserved;
  b[7] = compatibility;
  for (int i = 0; i < 8; ++i) b[8 + i] = uint8_t(stamp >> (56 - 8 * i));
  return b;
}

uint64_t AssetStampFor(const AvatarToc& toc) {
  return (toc.header().stamp & 0x0FFFFFFFFFFFFFFFull) | 0xC000000000000000ull;
}

std::optional<size_t> ResolveAsset(const AvatarToc& toc, const AssetId& id, std::string* error) {
  auto fail = [&](const char* why) -> std::optional<size_t> {
    if (error) *error = why;
    return std::nullopt;
  };
  if (id.stamp != AssetStampFor(toc)) return fail("asset ID stamp does not match the catalog");
  if (id.reserved != 0) return fail("asset ID byte 6 is not zero");
  if (id.index >= toc.size()) return fail("asset ID index beyond the catalog");
  const auto& rec = toc.record(id.index);
  if (rec.empty) return fail("asset ID names an unused catalog slot");
  if (rec.field_00 != id.category) return fail("asset ID category does not match the record");
  const auto opaque = toc.Opaque(id.index);  // record +0x04 is opaque[0]
  if (opaque[0] != id.compatibility) return fail("asset ID compatibility byte does not match the record");
  return size_t(id.index);
}

uint32_t BodyType(std::span<const uint8_t, kMetadataSize> metadata) {
  const uint8_t* body = metadata.data() + kBodyAssetAt;
  if (std::equal(kBodyType1.begin(), kBodyType1.end(), body)) return 1;
  if (std::equal(kBodyType2.begin(), kBodyType2.end(), body)) return 2;
  return 0;
}

AssetsResultSize ResultSizeFor(uint16_t component_mask) {
  AssetsResultSize s{(component_mask & 0x1000) ? 0x3147Cu : 0x1F80u, 0};
  for (int i = 12; i >= 0; --i) {
    if (component_mask & (1u << i)) {
      s.result += (kComponentSizes[i].result + 15) & ~15u;
      s.gpu_resources += (kComponentSizes[i].gpu + 4095) & ~4095u;
    }
  }
  return s;
}

}  // namespace x360n::avatar
