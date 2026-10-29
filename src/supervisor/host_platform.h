#pragma once
#include <string>

#include "paths.h"
namespace x360n {
std::string HostPlatform();
std::string ExecutableSuffix();
std::string ModuleSuffix();
std::string NativeEnvironmentValue(const char* name);
bool IsNativeModule(const std::filesystem::path& path);
std::filesystem::path NativeRuntimeFilename(const std::filesystem::path& path);
void ValidateNativeImage(const std::filesystem::path& path);
std::filesystem::path FindHostTool(const std::string& name);
std::filesystem::path NativeTitleExecutable(const ProjectPaths&,
                                            const std::string& title);
std::filesystem::path ResolveNativeExecutable(
    const ProjectPaths&, const std::string& title,
    const std::filesystem::path& configured);
// Cross-platform advisory leases; Windows additionally denies incompatible
// sharing. All session, title and SDK publishers use this same namespace.
class FileLease {
 public:
  FileLease(const std::filesystem::path&, bool exclusive, bool create = true);
  ~FileLease();
  FileLease(const FileLease&) = delete;
  FileLease& operator=(const FileLease&) = delete;

 private:
  intptr_t handle_ = -1;
};
}  // namespace x360n
