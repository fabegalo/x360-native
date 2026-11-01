#include "paths.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <system_error>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cerrno>
#include <functional>
#include <sstream>
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
  return fs::canonical(fs::path(buffer.data()));
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
fs::path LongPath(const fs::path& path) {
#ifdef _WIN32
  // The prefix disables Win32 normalization; GetFullPathNameW already applied it.
  auto name = fs::absolute(path).make_preferred().native();
  if (name.starts_with(L"\\\\?\\")) return name;
  return name.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + name.substr(2) : L"\\\\?\\" + name;
#else
  return path;
#endif
}
fs::path CanonicalPath(const fs::path& path) {
#ifdef _WIN32
  // Same resolution as the MSVC STL: open following links, ask for the final name.
  const auto name = LongPath(path);
  const auto file = CreateFileW(name.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    throw fs::filesystem_error("canonical", path, std::error_code(int(GetLastError()), std::system_category()));
  std::wstring final(512, L'\0');
  auto count = GetFinalPathNameByHandleW(file, final.data(), DWORD(final.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (count >= final.size()) {
    final.resize(size_t(count) + 1);
    count = GetFinalPathNameByHandleW(file, final.data(), DWORD(final.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  }
  const auto error = GetLastError();
  CloseHandle(file);
  if (!count || count >= final.size())
    throw fs::filesystem_error("canonical", path, std::error_code(int(error), std::system_category()));
  final.resize(count);
  if (final.starts_with(L"\\\\?\\UNC\\")) return fs::path(L"\\\\" + final.substr(8));
  if (final.starts_with(L"\\\\?\\") && final.size() > 6 && final[5] == L':') return fs::path(final.substr(4));
  return final;
#else
  return fs::canonical(path);
#endif
}
fs::path SharedSdkGenerationPath(const ProjectPaths& paths,
                                 const std::string& generation_id) {
  if (generation_id.empty() || generation_id.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
          std::string::npos)
    throw std::invalid_argument("Invalid shared SDK generation identity");
#ifdef _WIN32
  // SDL3 nests multiple CMakeTmp/try_compile trees, adding over 110
  // characters after the build root. The installation directory must never
  // determine whether Ninja can configure the SDK.
  const auto root_id = std::hash<std::wstring>{}(
      fs::absolute(paths.root).lexically_normal().native());
  const auto build_id = std::hash<std::string>{}(generation_id);
  std::ostringstream project_name, generation_name;
  project_name << "x360n-sdk-" << std::hex << root_id;
  generation_name << "g" << std::hex << build_id;
  const auto path = fs::temp_directory_path() / project_name.str() /
                    generation_name.str();
  // Reserve room for SDL3 nested try_compile, Ninja and object filenames.
  // An unusually deep TEMP must fail clearly rather than create a broken build.
  if (path.native().size() > 105)
    throw std::runtime_error(
        "Windows temporary directory is too deep for the shared SDK build; "
        "choose a shorter user TEMP directory");
  return path;
#else
  return paths.build() / "shared-sdk" / generation_id;
#endif
}

fs::path UserDataRoot(const ProjectPaths& paths, const fs::path& default_root) {
  const auto setting = paths.local() / "user-data-root.txt";
  if (!fs::exists(setting)) return default_root;
  std::ifstream in(setting, std::ios::binary); std::string value;
  if (!std::getline(in, value) || value.empty()) throw std::runtime_error("User data setting unreadable; select the storage folder again.");
  if (value.back() == '\r') value.pop_back();
  auto root = PathFromUtf8(value);
  if (!root.is_absolute()) throw std::runtime_error("User data path must be absolute.");
  for (const auto& protected_root : {paths.inputs(), paths.build(), paths.root / "titles", paths.sdk()}) {
    auto rel = fs::weakly_canonical(root).lexically_relative(fs::weakly_canonical(protected_root));
    if (!rel.empty() && !rel.is_absolute() && *rel.begin() != "..") throw std::runtime_error("User data cannot share content, source or build directories.");
  }
  return root / default_root.filename();
}
void SetUserDataRoot(const ProjectPaths& paths, const fs::path& directory) {
  if (!directory.is_absolute() || !fs::is_directory(directory)) throw std::runtime_error("Select an existing absolute storage folder.");
  for (const auto& protected_root : {paths.inputs(), paths.build(), paths.root / "titles", paths.sdk()}) {
    auto rel = fs::weakly_canonical(directory).lexically_relative(fs::weakly_canonical(protected_root));
    if (!rel.empty() && !rel.is_absolute() && *rel.begin() != "..") throw std::runtime_error("Select storage outside content, source and build folders.");
  }
  fs::create_directories(paths.local());
  const auto setting = paths.local() / "user-data-root.txt";
  auto staging = setting; staging += ".tmp";
  if (fs::exists(staging)) throw std::runtime_error("Unfinished storage setting found. Existing data was preserved.");
  { std::ofstream out(staging, std::ios::binary); out << PathUtf8(directory) << '\n'; out.flush(); if (!out) throw std::runtime_error("Cannot save storage selection."); }
#ifdef _WIN32
  if (!MoveFileExW(staging.c_str(), setting.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot commit storage selection.");
#else
  fs::rename(staging, setting);
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
  fs::path dir = fs::weakly_canonical(start, ec);
  if (ec) return std::nullopt;
  if (!fs::is_directory(dir, ec)) dir = dir.parent_path();
  while (true) {
    if (fs::exists(dir / "deps.lock.json", ec)) return ProjectPaths{dir};
    fs::path parent = dir.parent_path();
    if (parent == dir) return std::nullopt;
    dir = parent;
  }
}

bool IsWithin(const fs::path& path, const fs::path& base) {
  std::error_code ec;
  fs::path p = fs::canonical(path, ec);
  if (ec) return false;
  fs::path b = fs::canonical(base, ec);
  if (ec) return false;
  const auto ps = Folded(p), bs = Folded(b);
  if (ps.size() < bs.size() || ps.compare(0, bs.size(), bs) != 0) return false;
  // On POSIX, a backslash is a filename character, not a directory boundary.
  // A filesystem root already ends in its separator ("/" on POSIX).
  return ps.size() == bs.size() || bs.back() == fs::path::preferred_separator ||
         ps[bs.size()] == fs::path::preferred_separator;
}

std::optional<std::string> CheckLaunchable(const ProjectPaths& p, const fs::path& exe) {
  std::error_code ec;
  if (!fs::is_regular_file(exe, ec)) return "not an existing file: " + PathUtf8(exe);
#ifdef _WIN32
  std::wstring ext = exe.extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  if (ext != L".exe") return "only .exe build outputs can be launched: " + PathUtf8(exe);
#else
  if (access(exe.c_str(), X_OK) != 0) return "build output is not executable: " + PathUtf8(exe);
#endif
  if (fs::exists(p.inputs(), ec) && IsWithin(exe, p.inputs()))
    return "refusing to launch a file from .local/inputs (untrusted content): " + PathUtf8(exe);
  bool in_build = fs::exists(p.build(), ec) && IsWithin(exe, p.build());
  bool in_sdk_out = fs::exists(p.sdk_out(), ec) && IsWithin(exe, p.sdk_out());
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
