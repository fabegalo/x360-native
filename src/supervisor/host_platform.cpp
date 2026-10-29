#include "host_platform.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace x360n {
namespace fs = std::filesystem;
std::string NativeEnvironmentValue(const char* name) {
#ifdef _WIN32
  const auto key = PathFromUtf8(name).native();
  const auto count = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
  if (!count) return {};
  std::wstring value(count, L'\0');
  const auto written =
      GetEnvironmentVariableW(key.c_str(), value.data(), count);
  if (written >= count)
    throw std::runtime_error(
        "Environment changed while reading native toolchain");
  value.resize(written);
  return PathUtf8(fs::path(value));
#else
  const auto* value = std::getenv(name);
  return value ? value : "";
#endif
}
std::string HostPlatform() {
#ifdef _WIN32
  std::string os = "windows";
#elif defined(__APPLE__)
  std::string os = "macos";
#else
  std::string os = "linux";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
  return os + "-arm64";
#elif defined(_M_X64) || defined(__x86_64__)
  return os + "-x64";
#else
#error Unsupported native architecture
#endif
}
std::string ExecutableSuffix() {
#ifdef _WIN32
  return ".exe";
#else
  return {};
#endif
}
std::string ModuleSuffix() {
#ifdef _WIN32
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}
bool IsNativeModule(const fs::path& path) {
  auto name = PathUtf8(path.filename());
#ifdef _WIN32
  for (auto& c : name) if (c>='A' && c<='Z') c+= 'a'-'A';
#endif
  return name.ends_with(ModuleSuffix()) ||
         (ModuleSuffix() == ".so" && name.find(".so.") != std::string::npos);
}
fs::path NativeRuntimeFilename(const fs::path& path) {
#ifndef _WIN32
  if (path.extension() == ".dll")
    return path.parent_path() /
           ("lib" + PathUtf8(path.stem()) + ModuleSuffix());
#endif
  return path;
}
void ValidateNativeImage(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::array<unsigned char, 64> bytes{};
  file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  const auto count = file.gcount();
  const auto u16 = [&](unsigned i) {
    return unsigned(bytes[i]) | (unsigned(bytes[i + 1]) << 8);
  };
  [[maybe_unused]] const auto u32 = [&](unsigned i) {
    return unsigned(bytes[i]) | (unsigned(bytes[i + 1]) << 8) |
           (unsigned(bytes[i + 2]) << 16) | (unsigned(bytes[i + 3]) << 24);
  };
  bool valid = false;
#ifdef _WIN32
  if (count == 64 && bytes[0] == 'M' && bytes[1] == 'Z') {
    const auto offset = u32(60);
    file.clear();
    file.seekg(offset);
    file.read(reinterpret_cast<char*>(bytes.data()), 6);
    valid = file.gcount() == 6 && u32(0) == 0x4550 &&
            u16(4) == (HostPlatform().ends_with("-arm64") ? 0xAA64 : 0x8664);
  }
#elif defined(__APPLE__)
  valid =
      count >= 32 && u32(0) == 0xFEEDFACF &&
      u32(4) == (HostPlatform().ends_with("-arm64") ? 0x0100000C : 0x01000007);
#else
  valid = count >= 32 && bytes[0] == 0x7F && bytes[1] == 'E' &&
          bytes[2] == 'L' && bytes[3] == 'F' && bytes[4] == 2 &&
          bytes[5] == 1 &&
          u16(18) == (HostPlatform().ends_with("-arm64") ? 183u : 62u);
#endif
  if (!valid)
    throw std::runtime_error(
        "Native image OS/architecture mismatch: " + PathUtf8(path) +
        " (expected " + HostPlatform() + ")");
}
fs::path NativeTitleExecutable(const ProjectPaths& paths,
                               const std::string& title) {
#ifdef _WIN32
  if (HostPlatform() == "windows-x64")
    return paths.build() / "titles" / title / (title + ExecutableSuffix());
#endif
  return paths.build() / HostPlatform() / "titles" / title /
         (title + ExecutableSuffix());
}
fs::path FindHostTool(const std::string& name) {
  if (name.empty() || fs::path(name).has_parent_path())
    throw std::runtime_error("Invalid native tool name");
#ifdef _WIN32
  const auto wide = PathFromUtf8(name + ExecutableSuffix()).native();
  std::wstring result(32768, L'\0');
  const auto count = SearchPathW(nullptr, wide.c_str(), nullptr,
                                 DWORD(result.size()), result.data(), nullptr);
  if (count && count < result.size()) {
    result.resize(count);
    return result;
  }
#else
  const auto* path = std::getenv("PATH");
  const std::string search = path ? path : "/usr/bin:/bin";
  size_t start = 0;
  for (;;) {
    const auto end = search.find(':', start);
    auto directory = search.substr(start, end - start);
    // Do not implicitly search cwd for preparation tools.
    if (!directory.empty() && fs::path(directory).is_absolute()) {
      const auto candidate = fs::path(directory) / name;
      if (fs::is_regular_file(candidate) &&
          access(candidate.c_str(), X_OK) == 0)
        return fs::absolute(candidate).lexically_normal();
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
#endif
  throw std::runtime_error("Native tool missing from PATH: " + name);
}
fs::path ResolveNativeExecutable(const ProjectPaths& paths,
                                 const std::string& title,
                                 const fs::path& configured) {
#ifndef _WIN32
  if (configured.extension() == ".exe")
    return NativeTitleExecutable(paths, title);
#elif defined(_M_ARM64) || defined(__aarch64__)
  const auto relative = configured.lexically_normal().lexically_relative(
      paths.build() / "titles");
  if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
    return NativeTitleExecutable(paths, title);
#endif
  return configured;
}
FileLease::FileLease(const fs::path& path, bool exclusive, bool create) {
  if (create) fs::create_directories(path.parent_path());
#ifdef _WIN32
  const auto handle = CreateFileW(
      path.c_str(), GENERIC_READ | (create ? GENERIC_WRITE : 0),
      exclusive ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      create ? OPEN_ALWAYS : OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle != INVALID_HANDLE_VALUE) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(handle, &info) &&
        !(info.dwFileAttributes &
          (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
      handle_ = reinterpret_cast<intptr_t>(handle);
    else
      CloseHandle(handle);
  }
#else
  const int fd = open(
      path.c_str(),
      (create ? O_CREAT | O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd >= 0) {
    struct stat info{};
    if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
        flock(fd, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) == 0)
      handle_ = fd;
    else
      close(fd);
  }
#endif
  if (handle_ == -1)
    throw std::runtime_error("Environment or build busy / unsafe lease: " +
                             PathUtf8(path));
}
FileLease::~FileLease() {
  if (handle_ == -1) return;
#ifdef _WIN32
  CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
  close(int(handle_));
#endif
}
}  // namespace x360n
