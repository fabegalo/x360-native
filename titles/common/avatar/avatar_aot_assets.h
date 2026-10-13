#pragma once

#include <array>
#include <functional>
#include <initializer_list>
#include <vector>

#include "avatar_assets_request.h"
#include "avatar_metadata.h"

namespace rex::memory {
class Memory;
}

namespace x360n::avatar {

struct AssetMaterial {
  uint32_t id = 0;
  std::array<float, 4> rgba{};
};
struct AssetSelection {
  AssetId id{};
  uint32_t kind = 1, modifier_slot = 0;
  std::array<uint32_t, 3> colors{};
  std::array<AssetMaterial, 10> materials{};
};
struct AotAssetsStats {
  uint32_t allocations = 0, frees = 0, decoded = 0, applied = 0, finalized = 0;
  bool guards = true;
};
using AvatarAotCall =
    std::function<uint32_t(uint32_t, std::initializer_list<uint64_t>)>;

// Experimental 17559 backend: protocol 4, ordinary assets and their modifiers.
// Explicit selections use neutral shape; metadata selects height/weight/colors.
// Caller owns memory, catalog and stats and serializes access to the XAM profile.
// Empty destination allocates owned output; a supplied destination stays owned
// by the caller after Release. Scratch and source copies are always private.
std::unique_ptr<AssetsBackend> MakeAotAssetsBackend(
    rex::memory::Memory& memory, AvatarAotCall call, const AvatarToc& toc,
    std::vector<AssetSelection> selection, uint32_t protocol,
    uint32_t body_type, uint32_t callbacks, AotAssetsStats& stats,
    AssetsBuffers destination = {});

std::unique_ptr<AssetsBackend> MakeAotMetadataAssetsBackend(
    rex::memory::Memory& memory, AvatarAotCall call, const AvatarToc& toc,
    std::span<const uint8_t, kMetadataSize> metadata, uint16_t mask,
    uint32_t callbacks, AotAssetsStats& stats, AssetsBuffers destination = {});

}  // namespace x360n::avatar
