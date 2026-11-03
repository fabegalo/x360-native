#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "paths.h"
namespace x360n {
struct AotOptions {
  ProjectPaths paths;
  std::string title;
  std::filesystem::path build, sdk, cmake, ninja, cc, cxx;
  std::map<std::string, std::string> variables;
  std::chrono::seconds timeout{1800};
  bool keep_incomplete = false;
  bool shared_sdk = true;
  std::function<bool()> cancelled;
  std::function<void(const std::string&)> progress;
};
struct AotResult {
  bool reused = false;
  unsigned commands = 0;
  std::filesystem::path executable, receipt;
};
AotResult PrepareAot(AotOptions options);
int GuardNinja(const std::filesystem::path& configuration,
               const std::vector<std::string>& arguments,
               std::function<bool()> cancelled = {});
// The folder that preparation.toml [[files]] paths are relative to, as in the
// catalog: [x360n] game_data_root, else [project] game_root (a title whose
// codegen root is wider than its content names both).
std::filesystem::path RegisteredContentRoot(const std::filesystem::path& manifest);
// A title preset's cache value with ${sourceDir} and ${sourceParentDir} of
// `project` expanded. A value that starts with one of them and is a single
// path is normalized, so every title names a shared tool alike. Other macros
// are left for the caller to refuse.
std::string ExpandPresetValue(std::string text, const std::filesystem::path& project);
// Removes the shared SDK generations beside `current` (under the same owner)
// that no preparation can use again: a folder no pointer in `cache` names, or
// one whose receipt is missing or whose inputs changed, along with its
// pointer. Generations in use (their build lease held) and other owners'
// are kept. Returns the folder names removed.
std::vector<std::string> RetireSharedSdkGenerations(const std::filesystem::path& cache,
                                                    const std::filesystem::path& current);
}  // namespace x360n
