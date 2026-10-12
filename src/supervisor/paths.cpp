#include "paths.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <system_error>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace x360n {
std::string PathUtf8(const fs::path& path) {
  auto value = path.u8string(); return {value.begin(), value.end()};
}
fs::path PathFromUtf8(const std::string& value) {
  return fs::path(std::u8string(value.begin(), value.end()));
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

std::wstring Folded(const fs::path& p) {
  std::wstring s = p.lexically_normal().wstring();
#ifdef _WIN32
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  std::replace(s.begin(), s.end(), L'/', L'\\');
#endif
  while (s.size() > 1 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
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
  std::wstring ps = Folded(p), bs = Folded(b);
  if (ps.size() < bs.size() || ps.compare(0, bs.size(), bs) != 0) return false;
  return ps.size() == bs.size() || ps[bs.size()] == L'\\' || ps[bs.size()] == L'/';
}

std::optional<std::string> CheckLaunchable(const ProjectPaths& p, const fs::path& exe) {
  std::error_code ec;
  if (!fs::is_regular_file(exe, ec)) return "not an existing file: " + exe.string();
  std::wstring ext = exe.extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  if (ext != L".exe") return "only .exe build outputs can be launched: " + exe.string();
  if (fs::exists(p.inputs(), ec) && IsWithin(exe, p.inputs()))
    return "refusing to launch a file from .local/inputs (untrusted content): " + exe.string();
  bool in_build = fs::exists(p.build(), ec) && IsWithin(exe, p.build());
  bool in_sdk_out = fs::exists(p.sdk_out(), ec) && IsWithin(exe, p.sdk_out());
  if (!in_build && !in_sdk_out)
    return "executable is not a build output under .local/build or external/rexglue-sdk/out: " +
           exe.string();
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
