#include "avatar_aot_catalog.h"

#include <rex/memory.h>

#include <cstring>
#include <utility>

namespace x360n::avatar {
namespace {
constexpr uint32_t kSize = 0x20000, kGenerator = 0x1000, kPath = 0x400,
                   kOutput = 0x4000;
}
AotAvatarCatalog::AotAvatarCatalog(rex::memory::Memory& memory,
                                   AvatarAotCall call)
    : memory_(memory), call_(std::move(call)) {}
AotAvatarCatalog::~AotAvatarCatalog() { Close(); }
void AotAvatarCatalog::Close() {
  if (!workspace_) return;
  if (constructed_) call_(0x8197BFF0, {workspace_});
  memory_.SystemHeapFree(workspace_);
  workspace_ = 0;
  constructed_ = ready_ = false;
}
uint32_t AotAvatarCatalog::Open(const std::string& guest_path) {
  if (workspace_ || guest_path.empty() || guest_path.size() >= 260)
    return kAssetsInvalid;
  workspace_ = memory_.SystemHeapAlloc(kSize, 0x1000);
  if (!workspace_) return kAssetsNoMemory;
  auto* host = memory_.TranslateVirtual<uint8_t*>(workspace_);
  std::memset(host, 0, kSize);
  std::memcpy(host + kPath, guest_path.c_str(), guest_path.size() + 1);
  call_(0x8197D1B0, {workspace_});
  constructed_ = true;
  auto hr = call_(0x8197D060, {workspace_, workspace_ + kPath, 1});
  if (!hr) {
    call_(0x8197FD38, {workspace_ + kGenerator});
    hr = call_(0x8197FDA0, {workspace_ + kGenerator, workspace_});
  }
  ready_ = hr == 0;
  if (hr) Close();
  return hr;
}
uint32_t AotAvatarCatalog::Generate(
    uint32_t body_mask, uint32_t count,
    std::vector<std::array<uint8_t, kMetadataSize>>& result) {
  result.clear();
  if (!ready_ || !body_mask || (body_mask & ~3u) || !count || count > 100)
    return kAssetsInvalid;
  const auto hr = call_(0x8197FF28, {workspace_ + kGenerator, body_mask, count,
                                     workspace_ + kOutput});
  if (hr) return hr;
  const auto* output =
      memory_.TranslateVirtual<const uint8_t*>(workspace_ + kOutput);
  for (uint32_t i = 0; i < count; ++i) {
    std::array<uint8_t, kMetadataSize> metadata;
    std::memcpy(metadata.data(), output + i * kMetadataSize, kMetadataSize);
    if (!(BodyType(metadata) & body_mask)) {
      result.clear();
      return kAssetsFailure;
    }
    result.push_back(metadata);
  }
  return 0;
}
}  // namespace x360n::avatar
