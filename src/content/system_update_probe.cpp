#include "system_update.h"
#include "paths.h"
#include <stdexcept>

namespace fs = std::filesystem;
namespace x360n::content {
fs::path FindSystemUpdatePackage(const fs::path& directory) {
  auto folder = directory;
  const bool update_folder=fs::exists(folder / "$SystemUpdate");
  if (update_folder) folder /= "$SystemUpdate";
  if (!fs::is_directory(folder)) return {};
  if (fs::is_symlink(folder) || !IsWithin(folder, directory)) {
    if (update_folder) throw std::runtime_error("System Update: unsafe directory/link");
    return {};
  }
  fs::path result;
  size_t count = 0;
  for (const auto& entry : fs::directory_iterator(folder)) {
    if (++count > 4096) {
      if (update_folder || !result.empty()) throw std::runtime_error("System Update: too many directory entries");
      return {}; // leave ordinary large game folders to the existing importer
    }
    if (entry.is_symlink() || !IsWithin(entry.path(), folder)) {
      if (update_folder) throw std::runtime_error("System Update: unsafe entry/link");
      continue;
    }
    if (!entry.is_regular_file() || !IsSystemUpdatePackage(entry.path())) continue;
    if (!result.empty()) throw std::runtime_error("System Update: conflicting update packages");
    result = entry.path();
  }
  return result;
}
}
