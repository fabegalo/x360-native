#pragma once
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/null_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>

#include <filesystem>
#include <memory>

namespace dash17559 {
// The console's hard drive system extension partition holds the installed
// system packages and their system.manifest, read by the original XAM package
// registry (docs/avatar-system.md §20). The VFS resolves devices in registration
// order and the SDK's raw-partition NullDevice claims all of \Device\Harddisk0,
// so it is registered again after this mount.
inline bool MountSystemExtPartition(rex::filesystem::VirtualFileSystem& vfs,
                                    const std::filesystem::path& dashboard_root) {
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(
      "\\Device\\Harddisk0\\SystemExtPartition", dashboard_root / "systemext", true);
  const bool null_removed = vfs.UnregisterDevice("\\Device\\Harddisk0");
  bool ok = device->Initialize() && vfs.RegisterDevice(std::move(device));
  if (null_removed) {
    auto null_device = std::make_unique<rex::filesystem::NullDevice>(
        "\\Device\\Harddisk0",
        std::initializer_list<std::string>{"\\Partition0", "\\Cache0", "\\Cache1"});
    ok = null_device->Initialize() && vfs.RegisterDevice(std::move(null_device)) && ok;
  }
  if (!ok) REXLOG_ERROR("17559: could not mount the system extension partition");
  return ok;
}
}  // namespace dash17559
