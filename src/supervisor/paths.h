// Project layout and path validation. Everything machine-local lives under
// <root>/.local (ignored by git); user-supplied content goes in .local/inputs.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace x360n {
std::string PathUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(const std::string& value);

struct ProjectPaths {
  std::filesystem::path root;  // directory containing deps.lock.json
  std::filesystem::path local() const { return root / ".local"; }
  std::filesystem::path inputs() const { return local() / "inputs"; }
  std::filesystem::path runs() const { return local() / "evidence" / "runs"; }
  std::filesystem::path build() const { return local() / "build"; }
  std::filesystem::path sdk() const { return root / "external" / "rexglue-sdk"; }
  std::filesystem::path sdk_out() const { return sdk() / "out"; }
};

// Walks up from `start` looking for deps.lock.json.
std::optional<ProjectPaths> FindProjectRoot(const std::filesystem::path& start);

// True if `path` resolves (after following existing symlinks/junctions) to a
// location inside `base`. Both must exist. Lexical tricks such as ".." or
// differing case on Windows are handled by weakly_canonical + case folding.
bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& base);

// Decides whether the supervisor may launch `exe`: it must be an existing .exe
// produced by a build (under .local/build or the SDK's out/), and never
// anything under .local/inputs, where untrusted game/dashboard data lives.
// Returns an error message, or nullopt if allowed.
std::optional<std::string> CheckLaunchable(const ProjectPaths& p, const std::filesystem::path& exe);

// Characters safe for a run label (used in a directory name).
bool IsSafeLabel(const std::string& label);

// One path component read from a content package (STFS entry name). Rejects
// anything that could escape the extraction directory or alias a Windows
// device: empty, ".", "..", separators, ':', wildcards, control characters,
// trailing space/dot, reserved names (CON, NUL, COM1...).
bool IsSafePackageEntryName(const std::string& name);
// Optional user-selected storage, shared across source/package versions.
// Defaults remain unchanged for existing environments without this setting.
std::filesystem::path UserDataRoot(const ProjectPaths& paths,
                                   const std::filesystem::path& default_root);
void SetUserDataRoot(const ProjectPaths& paths, const std::filesystem::path& directory);

}  // namespace x360n
