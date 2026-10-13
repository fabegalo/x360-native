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
  std::array<AssetMaterial, 10> materials{};
};
struct AotAssetsStats {
  uint32_t allocations = 0, frees = 0, decoded = 0, applied = 0, finalized = 0;
  bool guards = true;
};
using AvatarAotCall =
    std::function<uint32_t(uint32_t, std::initializer_list<uint64_t>)>;

// Experimental 17559 backend: explicit protocol 4, ordinary component assets,
// neutral body shape. Not an SDK export. Caller owns memory, catalog and stats
// for the request's entire lifetime and invokes it on one guest thread.
std::unique_ptr<AssetsBackend> MakeAotAssetsBackend(
    rex::memory::Memory& memory, AvatarAotCall call, const AvatarToc& toc,
    std::vector<AssetSelection> selection, uint32_t protocol,
    uint32_t body_type, uint32_t callbacks, AotAssetsStats& stats);

}  // namespace x360n::avatar
