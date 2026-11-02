#include "paths.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <system_error>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cerrno>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;

namespace x360n {
uint32_t CurrentProcessId() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return uint32_t(getpid());
#endif
}

fs::path CurrentExecutablePath() {
#ifdef _WIN32
  std::wstring buffer(1024, L'\0');
  for (;;) {
    const auto count = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (!count) throw std::system_error(int(GetLastError()), std::system_category(), "GetModuleFileNameW");
    if (count < buffer.size()) { buffer.resize(count); return fs::path(buffer); }
    if (buffer.size() >= 32768) throw std::runtime_error("Executable path exceeds Windows path limit");
    buffer.resize(std::min<size_t>(buffer.size() * 2, 32768));
  }
#elif defined(__APPLE__)
  uint32_t count = 0;
  _NSGetExecutablePath(nullptr, &count);
  std::vector<char> buffer(count);
  if (buffer.empty() || _NSGetExecutablePath(buffer.data(), &count) != 0)
    throw std::runtime_error("Could not resolve native executable path");
  return io::canonical(fs::path(buffer.data()));
#else
  std::string buffer(1024, '\0');
  for (;;) {
    const auto count = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (count < 0) throw std::system_error(errno, std::generic_category(), "readlink /proc/self/exe");
    if (size_t(count) < buffer.size()) { buffer.resize(size_t(count)); return fs::path(buffer); }
    if (buffer.size() >= 1024 * 1024) throw std::runtime_error("Executable path exceeds diagnostic limit");
    buffer.resize(buffer.size() * 2);
  }
#endif
}

std::string PathUtf8(const fs::path& path) {
  auto value = path.u8string(); return {value.begin(), value.end()};
}
fs::path PathFromUtf8(const std::string& value) {
  return fs::path(std::u8string(value.begin(), value.end()));
}
fs::path LongPath(const fs::path& path) { return host_path::Extended(path); }
fs::path OrdinaryPath(const fs::path& path) { return host_path::Ordinary(path); }
fs::path CanonicalPath(const fs::path& path) { return io::canonical(path); }
namespace {
// FNV-1a over the UTF-8 installation path, independent of std::hash's
// implementation/CRT and stable across processes and updates.
std::string ShortProjectId(const fs::path& path) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : PathUtf8(fs::absolute(path).lexically_normal())) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  constexpr char digits[] = "0123456789abcdef";
  std::string id(16, '0');
  for (int i = 15; i >= 0; --i) {
    id[size_t(i)] = digits[hash & 15];
    hash >>= 4;
  }
  return id;
}
}  // namespace

fs::path SharedSdkBuildRoot(const ProjectPaths& paths) {
#ifdef _WIN32
  // Use the current user's persistent local application data, not TEMP (which
  // may be deleted automatically) and not the install folder (which can be
  // arbitrarily deep). Avoid choosing a different drive via filesystem root.
  std::wstring local;
  if (const wchar_t* value = _wgetenv(L"LOCALAPPDATA"); value && *value)
    local = value;
  else
    local = fs::temp_directory_path().wstring();
  const auto root = fs::path(local) / "x360n" / "s" / ShortProjectId(paths.root);
  // SDL3's nested try_compile adds 113+ characters to the CMake binary dir.
  // Fail before making a broken cache if the OS profile itself is too deep.
  if (root.wstring().size() > 80)
    throw std::runtime_error("Windows user-local application-data path too deep "
                             "for CMake/Ninja shared SDK; select a shorter build "
                             "cache location");
  return root;
#else
  return paths.build() / "shared-sdk";
#endif
}

fs::path NativeBuildRoot(const ProjectPaths& paths) {
#ifdef _WIN32
  return SharedSdkBuildRoot(paths).parent_path().parent_path() / "b";
#else
  return paths.build();
#endif
}

fs::path NativeBuildDirectory(const ProjectPaths& paths, const fs::path& published) {
#ifdef _WIN32
  // <local>\x360n\b\<id>. A generic title's deepest object lies ~190
  // characters below it; fail before making a tree Ninja cannot build.
  const auto tree = NativeBuildRoot(paths) / ShortProjectId(io::weakly_canonical(published));
  if (tree.native().size() > 69)
    throw std::runtime_error("Windows user-local application-data path too deep "
                             "for CMake/Ninja title builds; select a shorter build "
                             "cache location");
  return tree;
#else
  (void)paths;
  return published;
#endif
}

fs::path SharedSdkGenerationPath(const ProjectPaths& paths,
                                 const std::string& generation_id) {
  if (generation_id.empty() || generation_id.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
          std::string::npos)
    throw std::invalid_argument("Invalid shared SDK generation identity");
#ifdef _WIN32
  const auto path = SharedSdkBuildRoot(paths) / generation_id;
  if (path.wstring().size() > 104)
    throw std::runtime_error("Windows shared SDK generation path exceeds "
                             "the safe CMake/Ninja depth");
  return path;
#else
  return SharedSdkBuildRoot(paths) / generation_id;
#endif
}

fs::path UserDataRoot(const ProjectPaths& paths, const fs::path& default_root) {
  const auto setting = paths.local() / "user-data-root.txt";
  if (!io::exists(setting)) return default_root;
  io::ifstream in(setting, std::ios::binary); std::string value;
  if (!std::getline(in, value) || value.empty()) throw std::runtime_error("User data setting unreadable; select the storage folder again.");
  if (value.back() == '\r') value.pop_back();
  auto root = PathFromUtf8(value);
  if (!root.is_absolute()) throw std::runtime_error("User data path must be absolute.");
  for (const auto& protected_root : {paths.inputs(), paths.build(), paths.root / "titles", paths.sdk()}) {
    auto rel = io::weakly_canonical(root).lexically_relative(io::weakly_canonical(protected_root));
    if (!rel.empty() && !rel.is_absolute() && *rel.begin() != "..") throw std::runtime_error("User data cannot share content, source or build directories.");
  }
  return root / default_root.filename();
}
void SetUserDataRoot(const ProjectPaths& paths, const fs::path& directory) {
  if (!directory.is_absolute() || !io::is_directory(directory)) throw std::runtime_error("Select an existing absolute storage folder.");
  for (const auto& protected_root : {paths.inputs(), paths.build(), paths.root / "titles", paths.sdk()}) {
    auto rel = io::weakly_canonical(directory).lexically_relative(io::weakly_canonical(protected_root));
    if (!rel.empty() && !rel.is_absolute() && *rel.begin() != "..") throw std::runtime_error("Select storage outside content, source and build folders.");
  }
  io::create_directories(paths.local());
  const auto setting = paths.local() / "user-data-root.txt";
  auto staging = setting; staging += ".tmp";
  if (io::exists(staging)) throw std::runtime_error("Unfinished storage setting found. Existing data was preserved.");
  { io::ofstream out(staging, std::ios::binary); out << PathUtf8(directory) << '\n'; out.flush(); if (!out) throw std::runtime_error("Cannot save storage selection."); }
#ifdef _WIN32
  if (!MoveFileExW(io::Host(staging).c_str(), io::Host(setting).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot commit storage selection.");
#else
  io::rename(staging, setting);
#endif
}
namespace {

fs::path::string_type Folded(const fs::path& p) {
  auto s = p.lexically_normal().native();
#ifdef _WIN32
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  std::replace(s.begin(), s.end(), L'/', L'\\');
#endif
  while (s.size() > 1 && s.back() == fs::path::preferred_separator) s.pop_back();
  return s;
}

}  // namespace

std::optional<ProjectPaths> FindProjectRoot(const fs::path& start) {
  std::error_code ec;
  fs::path dir = io::weakly_canonical(start, ec);
  if (ec) return std::nullopt;
  if (!io::is_directory(dir, ec)) dir = dir.parent_path();
  while (true) {
    if (io::exists(dir / "deps.lock.json", ec)) return ProjectPaths{dir};
    fs::path parent = dir.parent_path();
    if (parent == dir) return std::nullopt;
    dir = parent;
  }
}

bool IsWithin(const fs::path& path, const fs::path& base) {
  std::error_code ec;
  fs::path p = io::canonical(LongPath(path), ec);
  if (ec) return false;
  fs::path b = io::canonical(LongPath(base), ec);
  if (ec) return false;
  // fs::canonical retains the extended-length prefix; compare ordinary spellings.
  p = OrdinaryPath(p); b = OrdinaryPath(b);
  const auto ps = Folded(p), bs = Folded(b);
  if (ps.size() < bs.size() || ps.compare(0, bs.size(), bs) != 0) return false;
  // On POSIX, a backslash is a filename character, not a directory boundary.
  // A filesystem root already ends in its separator ("/" on POSIX).
  return ps.size() == bs.size() || bs.back() == fs::path::preferred_separator ||
         ps[bs.size()] == fs::path::preferred_separator;
}

std::optional<std::string> CheckLaunchable(const ProjectPaths& p, const fs::path& exe) {
  std::error_code ec;
  if (!io::is_regular_file(exe, ec)) return "not an existing file: " + PathUtf8(exe);
#ifdef _WIN32
  std::wstring ext = exe.extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  if (ext != L".exe") return "only .exe build outputs can be launched: " + PathUtf8(exe);
#else
  if (access(exe.c_str(), X_OK) != 0) return "build output is not executable: " + PathUtf8(exe);
#endif
  if (io::exists(p.inputs(), ec) && IsWithin(exe, p.inputs()))
    return "refusing to launch a file from .local/inputs (untrusted content): " + PathUtf8(exe);
  bool in_build = io::exists(p.build(), ec) && IsWithin(exe, p.build());
  bool in_sdk_out = io::exists(p.sdk_out(), ec) && IsWithin(exe, p.sdk_out());
  if (!in_build && !in_sdk_out)
    return "executable is not a build output under .local/build or external/rexglue-sdk/out: " +
           PathUtf8(exe);
  return std::nullopt;
}

bool IsSafePackageEntryName(const std::string& name) {
  if (name.empty() || name == "." || name == ".." || name.size() > 255) return false;
  for (unsigned char c : name) {
    if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|')
      return false;
  }
  if (name.back() == ' ' || name.back() == '.') return false;
  std::string stem = name.substr(0, name.find('.'));
  for (auto& ch : stem) ch = char(std::toupper(static_cast<unsigned char>(ch)));
  static const char* kReserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                    "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                    "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
  for (const char* r : kReserved)
    if (stem == r) return false;
  return true;
}

bool IsSafeLabel(const std::string& label) {
  if (label.empty() || label.size() > 64) return false;
  return std::all_of(label.begin(), label.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
  }) && label.find("..") == std::string::npos;
}

}  // namespace x360n
