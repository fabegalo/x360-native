#pragma once

#include <string>

#include "avatar_aot_assets.h"

namespace x360n::avatar {
// Original 17559 reader and random metadata generator, with caller-owned
// workspaces. Opens only a previously mounted read-only catalog path. No XAM
// app manager, global heap or module entry point is initialized.
class AotAvatarCatalog {
 public:
  AotAvatarCatalog(rex::memory::Memory& memory, AvatarAotCall call);
  ~AotAvatarCatalog();
  AotAvatarCatalog(const AotAvatarCatalog&) = delete;
  AotAvatarCatalog& operator=(const AotAvatarCatalog&) = delete;
  uint32_t Open(const std::string& guest_path);
  uint32_t Generate(uint32_t body_mask, uint32_t count,
                    std::vector<std::array<uint8_t, kMetadataSize>>& result);
  void Close();

 private:
  rex::memory::Memory& memory_;
  AvatarAotCall call_;
  uint32_t workspace_ = 0;
  bool constructed_ = false, ready_ = false;
};
}  // namespace x360n::avatar
