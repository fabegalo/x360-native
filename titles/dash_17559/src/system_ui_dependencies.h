#pragma once
#include <string>
#include <string_view>

namespace dash17559 {
// The host has already verified xam_ui.xex and vk.xex against the 17559
// manifest. Resolve this dependency from that exact module's namespace, never
// the foreground game's partition or a basename search across dashboards.
inline std::string OriginalKeyboardDependency(std::string_view xam_path) {
  // UserModule::LoadFromFile records the resolved device path, rather than
  // the symbolic path the host used to request the verified original image.
  if (xam_path == "\\SystemRoot\\system\\xam_ui.xex") return "sys17559:\\vk.xex";
  if (xam_path == "\\Device\\Harddisk0\\Partition1\\system\\xam_ui.xex") return "game:\\vk.xex";
  if (xam_path == "sys17559:\\system\\xam_ui.xex") return "sys17559:\\vk.xex";
  if (xam_path == "game:\\system\\xam_ui.xex") return "game:\\vk.xex";
  return {};
}
}
