#include "system_update.h"
#include "paths.h"
#include <stdexcept>

namespace fs = std::filesystem;
namespace x360n::content {
fs::path FindSystemUpdatePackage(const fs::path& directory) {
  auto folder = directory;
  const bool update_folder=io::exists(folder / "$SystemUpdate");
  if (update_folder) folder /= "$SystemUpdate";
  if (!io::is_directory(folder)) return {};
  if (io::is_redirect(folder) || !IsWithin(folder, directory)) {
    if (update_folder) throw std::runtime_error("System Update: unsafe directory/link");
    return {};
  }
  fs::path result;
  size_t count = 0;
  for (const auto& entry : io::directory_iterator(folder)) {
    if (++count > 4096) {
      if (update_folder || !result.empty()) throw std::runtime_error("System Update: too many directory entries");
      return {}; // leave ordinary large game folders to the existing importer
    }
    if (entry.is_redirect() || !IsWithin(entry.path(), folder)) {
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
