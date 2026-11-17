#pragma once
#include <rex/filesystem/file_system.h>
#include <rex/string/text.h>
#include <rex/logging/log_category.h>

#include <filesystem>
#include <algorithm>

namespace dash17559 {
// The console's hard drive system extension partition holds the installed
// system packages and their system.manifest, read by the original XAM package
// registry (docs/avatar-system.md §20). The filesystem resolves mounts in registration
// order and the SDK's raw-partition null provider claims all of \Device\Harddisk0,
// so it is registered again after this mount.
inline bool MountSystemExtPartition(rex::filesystem::IFileSystem& vfs,
                                    const std::filesystem::path& dashboard_root) {
  using namespace rex::filesystem;
  const auto mounts = vfs.GetMounts();
  const auto raw = std::ranges::find_if(mounts, [](const auto& mount) {
    return rex::string::utf8_equal_case(mount.path, "\\Device\\Harddisk0");
  });
  if (raw != mounts.end() && raw->type != ProviderType::kNull) {
    REXLOG_ERROR("17559: Harddisk0 already has a non-null owner");
    return false;
  }
  const char* path = "\\Device\\Harddisk0\\SystemExtPartition";
  // Validate the original folder before changing the existing raw-partition mount.
  if (!vfs.Mount({path, ProviderType::kHostDirectory, dashboard_root / "systemext",
                  MountFlags::kNone, {}})) {
    REXLOG_ERROR("17559: could not mount the system extension partition");
    return false;
  }
  if (raw != mounts.end()) {
    if (!vfs.Unmount(raw->path)) {
      const auto rollback = vfs.Unmount(path);
      if (!rollback) REXLOG_ERROR("17559: could not roll back the system extension mount");
      return false;
    }
    if (!vfs.Mount(*raw)) {
      REXLOG_ERROR("17559: could not restore the raw-partition mount");
      return false;
    }
  }
  return true;
}
}  // namespace dash17559
