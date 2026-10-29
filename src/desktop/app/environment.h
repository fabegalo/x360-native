#pragma once
// Host services shared by the launcher on every system. Each system implements
// them natively (environment_win32.cpp, environment_posix.cpp); the controller
// and the pages only see this interface.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace x360n::desktop {
struct Environment {
  // graphics: the default session API of this host can create a device
  // (Windows D3D12, Linux/macOS Vulkan). vulkan: a Vulkan device is available.
  bool graphics = false, d3d12 = false, vulkan = false;
  bool writable = false, architecture = false;
  // Native preparation toolchain. Windows: MSVC 2022 + Windows SDK beside the
  // pinned LLVM; Linux/macOS: clang, clang++, cmake, ninja and git on PATH.
  bool toolchain = false;
  std::vector<std::string> missing_tools;
  uintmax_t available = 0;
  std::string cpu, gpu, memory;
  std::string description;
};
Environment CheckEnvironment(const std::filesystem::path& root);
// Vulkan instance/device probe through the system loader, loaded on demand so
// the launcher still opens when no loader or driver exists. Empty: none.
std::string ProbeVulkanDevice();
// Common facts every implementation reports the same way.
void DescribeStorage(Environment& environment, const std::filesystem::path& root);

// A system program by name: System32 on Windows, /usr/bin (then /bin) elsewhere.
std::filesystem::path SystemTool(const std::string& name);
std::filesystem::path SelfExecutable();
unsigned long CurrentProcess();
// Opens a folder or file with the system shell (Explorer, Finder, xdg-open).
bool OpenPath(const std::filesystem::path& path);
// Starts a new launcher process; false when it could not be started.
bool StartDetached(const std::filesystem::path& exe);
// Native preparation command: on Windows the pinned developer environment
// (scripts/devshell.cmd) wraps the supervisor; elsewhere it runs directly.
struct Command { std::filesystem::path exe; std::vector<std::string> args; };
Command PreparationCommand(const std::filesystem::path& root, const std::filesystem::path& supervisor,
                           const std::vector<std::string>& supervisor_args);
}  // namespace x360n::desktop
