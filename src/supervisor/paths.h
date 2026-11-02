// Project layout and path validation. Everything machine-local lives under
// <root>/.local (ignored by git); user-supplied content goes in .local/inputs.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <cstdint>

#include "fs_io.h"

namespace x360n {

// Actual native process identity; independent of the caller's working directory.
std::filesystem::path CurrentExecutablePath();
uint32_t CurrentProcessId();
std::string PathUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(const std::string& value);
// Name accepted by Win32 file APIs beyond MAX_PATH (\\?\ form of the absolute
// path); unchanged on other hosts. Packaged environments live under arbitrary
// user folders, where SDK test data alone exceeds 260 characters.
std::filesystem::path LongPath(const std::filesystem::path& path);
// Drive/UNC spelling without the \\?\ (or \\?\UNC\) prefix; anything else is
// unchanged. MSVC parses "\\?" as the root name, so walking the parents of an
// extended path reaches "\\?\C:", which Win32 rejects (ERROR_INVALID_PARAMETER).
std::filesystem::path OrdinaryPath(const std::filesystem::path& path);
// fs::canonical, also beyond MAX_PATH; the result never carries the \\?\ prefix.
std::filesystem::path CanonicalPath(const std::filesystem::path& path);

struct ProjectPaths {
  std::filesystem::path root;  // directory containing deps.lock.json
  std::filesystem::path local() const { return root / ".local"; }
  std::filesystem::path inputs() const { return local() / "inputs"; }
  std::filesystem::path runs() const { return local() / "evidence" / "runs"; }
  std::filesystem::path build() const { return local() / "build"; }
  std::filesystem::path sdk() const { return root / "external" / "rexglue-sdk"; }
  std::filesystem::path sdk_out() const { return sdk() / "out"; }
};

// User-local, persistent, per-installation build owner. Windows uses a short
// path outside the installation to avoid SDL3's nested CMake try_compile limit.
// Linux/macOS keep their existing project-local shared-SDK build owner.
std::filesystem::path SharedSdkBuildRoot(const ProjectPaths& paths);
// Parent of every NativeBuildDirectory (Windows: <local>\x360n\b; elsewhere
// the project build directory).
std::filesystem::path NativeBuildRoot(const ProjectPaths& paths);
// CMake/Ninja binary directory of the native build whose products are
// published in `published` (a title executable's directory). Windows: a short
// directory beside the shared SDK generations, named by `published`, because
// Ninja and CMake fail once their working directory plus an object path pass
// MAX_PATH and a title's own objects add about 190 characters. Elsewhere the
// build stays in `published`.
std::filesystem::path NativeBuildDirectory(const ProjectPaths& paths,
                                           const std::filesystem::path& published);
// A unique, short generation inside the shared SDK owner. Source, game data,
// title outputs, and verified receipts retain their established locations.
std::filesystem::path SharedSdkGenerationPath(const ProjectPaths& paths,
                                              const std::string& generation_id);

// Walks up from `start` looking for deps.lock.json.
std::optional<ProjectPaths> FindProjectRoot(const std::filesystem::path& start);

// True if `path` resolves (after following existing symlinks/junctions) to a
// location inside `base`. Both must exist. Lexical tricks such as ".." or
// differing case on Windows are handled by weakly_canonical + case folding.
bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& base);

// The spelling a child process can start in for `directory`. Windows caps a
// process working directory at MAX_PATH - 2 characters unless the machine
// opts in to long paths, and 8.3 aliases may not exist. A deeper folder inside
// an installation is reached through <local>\x360n\r\<id>, a verified
// junction to that installation's root (no privilege, no registry), so
// relative paths, ".." included, still resolve inside the installation. Throws
// for a deeper folder outside an installation or on a network share. Elsewhere
// the directory is returned unchanged.
std::filesystem::path ChildWorkingDirectory(const std::filesystem::path& directory);

// The spelling a child process is started by for the executable `exe`: the
// ordinary name where it fits MAX_PATH (.NET Framework hosts such as Windows
// PowerShell 5.1 fail to start from an extended one), else the installation
// alias as for the working directory, else the extended name.
std::filesystem::path ChildExecutable(const std::filesystem::path& exe);

// The spelling of `path` that preparation tools get. Windows PowerShell 5.1,
// cmd, Ninja and the compilers cannot open a name beyond MAX_PATH unless the
// machine opts in to long paths, and they reach names up to ~180 characters
// below the installation root (the SDK's third-party trees). Paths inside an
// installation whose root leaves less room go through the same verified
// alias; everything else (POSIX included) keeps its spelling. Throws for an
// installation whose root is longer than PreparationRootLimit().
std::filesystem::path ToolPath(const std::filesystem::path& path);

// The longest installation root, in UTF-8 bytes of its canonical name, that
// can prepare games. Windows: both resolve the alias to the real name. Ninja
// refuses names over MAX_PATH bytes, and CMake names its own modules up to 119
// characters below the root. Git opens the SDK repository's metadata, up to
// 102 characters below it, before it applies core.longpaths. Elsewhere
// unlimited.
size_t PreparationRootLimit();

// Decides whether the supervisor may launch `exe`: an existing native executable
// (.exe on Windows, executable permission on POSIX),
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
