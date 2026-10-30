#include <fstream>
#include <spawn.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#else
#include <sys/sysinfo.h>
#endif
#include "environment.h"
#include "handheld.h"
#include "host_platform.h"
extern char** environ;
namespace fs = std::filesystem;
namespace x360n::desktop {
fs::path SystemTool(const std::string& name) {
  for (const char* dir : {"/usr/bin", "/bin"})
    if (fs::is_regular_file(fs::path(dir) / name)) return fs::path(dir) / name;
  return fs::path("/usr/bin") / name;
}
fs::path SelfExecutable() {
#ifdef __APPLE__
  uint32_t size = 0; _NSGetExecutablePath(nullptr, &size);
  std::string path(size, '\0');
  if (_NSGetExecutablePath(path.data(), &size) != 0) return {};
  return fs::weakly_canonical(path.c_str());
#else
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec);
#endif
}
unsigned long CurrentProcess() { return static_cast<unsigned long>(getpid()); }
bool OpenPath(const fs::path& path) {
#ifdef __APPLE__
  const char* opener = "open";
#else
  const char* opener = "xdg-open";
#endif
  pid_t child = 0;
  const auto text = path.string();
  char* argv[] = {const_cast<char*>(opener), const_cast<char*>(text.c_str()), nullptr};
  if (posix_spawnp(&child, opener, nullptr, nullptr, argv, environ) != 0) return false;
  // Reap the opener when it exits; it never outlives a moment.
  std::thread([child] { int status = 0; waitpid(child, &status, 0); }).detach();
  return true;
}
bool StartDetached(const fs::path& exe) {
  pid_t child = 0;
  const auto path = exe.string();
  char* argv[] = {const_cast<char*>(path.c_str()), nullptr};
  return posix_spawn(&child, path.c_str(), nullptr, nullptr, argv, environ) == 0;
}
Command PreparationCommand(const fs::path&, const fs::path& supervisor, const std::vector<std::string>& supervisor_args) {
  return {supervisor, supervisor_args};
}
Environment CheckEnvironment(const fs::path& root) {
  Environment r;
  utsname name{};
  uname(&name);
  const std::string machine = name.machine;
#ifdef __APPLE__
  r.architecture = machine == "arm64" || machine == "x86_64";
  char brand[256]{}; size_t size = sizeof(brand);
  if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0) r.cpu = brand;
  uint64_t bytes = 0; size = sizeof(bytes);
  if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0) r.memory = std::to_string((bytes + (512ull << 20)) >> 30) + " GiB";
  r.description = std::string("macOS ") + machine + "\r\n";
#else
  r.architecture = machine == "x86_64" || machine == "aarch64";
  std::ifstream cpuinfo("/proc/cpuinfo");
  for (std::string line; std::getline(cpuinfo, line);)
    if (line.starts_with("model name")) { const auto colon = line.find(':'); if (colon != std::string::npos) r.cpu = line.substr(colon + 2); break; }
  struct sysinfo info{};
  if (sysinfo(&info) == 0) r.memory = std::to_string((uint64_t(info.totalram) * info.mem_unit + (512ull << 20)) >> 30) + " GiB";
  r.description = std::string("Linux ") + machine + "\r\n";
  // Steam Deck LCD/OLED and Gaming Mode: the same package, reported for diagnostics.
  r.description += DescribeHandheld(DetectHandheld());
#endif
  if (!r.architecture) r.description += "Unsupported architecture / arquitetura não validada\r\n";
  r.gpu = ProbeVulkanDevice();
  r.vulkan = !r.gpu.empty();
  r.graphics = r.vulkan;
  r.description += r.vulkan ? "Vulkan: " + r.gpu + "\r\n" : "Vulkan device unavailable / dispositivo indisponível\r\n";
  if (root.empty()) return r;
  DescribeStorage(r, root);
  // The supervisor's native preparation resolves exactly these tools from PATH.
  for (const char* tool : {"clang", "clang++", "cmake", "ninja", "git"}) {
    try { (void)FindHostTool(tool); }
    catch (const std::exception&) { r.missing_tools.push_back(tool); }
  }
  r.toolchain = r.missing_tools.empty();
  std::string missing;
  for (const auto& tool : r.missing_tools) missing += (missing.empty() ? "" : ", ") + tool;
  r.description += r.toolchain ? "Toolchain (clang, cmake, ninja, git): OK\r\n" :
      "Toolchain missing / ausente: " + missing + "\r\n";
  return r;
}
}  // namespace x360n::desktop
