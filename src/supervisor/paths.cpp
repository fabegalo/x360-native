#include "paths.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <system_error>

namespace fs = std::filesystem;

namespace x360n {
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

bool IsSafeLabel(const std::string& label) {
  if (label.empty() || label.size() > 64) return false;
  return std::all_of(label.begin(), label.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
  }) && label.find("..") == std::string::npos;
}

}  // namespace x360n
