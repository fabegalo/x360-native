// Avatar asset IDs, the 1000-byte avatar description (X_AVATAR_METADATA) and the
// two XAM computations that depend only on them. Evidence: docs/avatar-system.md
// §§6-7 (XAM 2.0.17559.0 read as reference; original descriptors Avatar64xx.Avatar).
//
// Asset ID (16 bytes, big-endian), as built by XAM 0x8197E240 and found in the
// original descriptors (21 of 21 resolve to the right catalog record):
//   +0  u32  category mask      == catalog record field_00
//   +4  u16  catalog record index
//   +6  u8   0
//   +7  u8   compatibility byte  == catalog record byte +4 (1 male, 2 female, 3 both:
//            hypothesis from the MaleBody/FemaleBody records)
//   +8  u64  catalog stamp with the top nibble set to 0xC
//
// Description layout (only what is known): +0x0C..+0xFB fifteen 16-byte slots that
// hold component asset IDs (or zeros); +0xFC..+0x11F nine ARGB colors; +0x120 the
// body asset ID. Other fields are not interpreted.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "avatar_toc.h"

namespace x360n::avatar {

constexpr size_t kMetadataSize = 1000;
constexpr size_t kComponentSlotsAt = 0x0C;
constexpr size_t kComponentSlotCount = 15;
constexpr size_t kBodyAssetAt = 0x120;

struct AssetId {
  uint32_t category;
  uint16_t index;
  uint8_t reserved;
  uint8_t compatibility;
  uint64_t stamp;

  static AssetId FromBytes(std::span<const uint8_t, 16> b);
  std::array<uint8_t, 16> ToBytes() const;
  bool IsZero() const { return category == 0 && index == 0 && reserved == 0 && compatibility == 0 && stamp == 0; }
};

// The stamp an asset ID of this catalog carries: header stamp, top nibble 0xC
// (XAM 0x8197E240: clrldi 4, then or 3 << 62).
uint64_t AssetStampFor(const AvatarToc& toc);

// Catalog record an asset ID names, or nothing (with *error) if the stamp, index,
// category or compatibility byte does not match a used record.
std::optional<size_t> ResolveAsset(const AvatarToc& toc, const AssetId& id, std::string* error);

// XamAvatarManifestGetBodyType (XAM 0x816753C8 -> 0x8196CA80): 1 if the body ID at
// +0x120 equals the constant at XAM 0x81632E68, 2 for the one at 0x81632E78, else 0.
uint32_t BodyType(std::span<const uint8_t, kMetadataSize> metadata);

// XamAvatarGetAssetsResultSize (XAM 0x81965EC0 with the 13-entry table at
// 0x816332B8): sizes of the result and GPU-resource buffers for a component mask.
struct AssetsResultSize {
  uint32_t result;
  uint32_t gpu_resources;
};
AssetsResultSize ResultSizeFor(uint16_t component_mask);

}  // namespace x360n::avatar
