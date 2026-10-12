#pragma once
#include "paths.h"
#include <functional>
#include <vector>

namespace x360n::desktop {
struct Input {
  std::filesystem::path destination;
  std::string source_name, sha256;
};
struct Target {
  std::string id, label, limitations;
  std::filesystem::path manifest, game_root, user_root, exe;
  std::vector<Input> inputs;
  std::vector<std::string> dependencies;
  std::vector<std::filesystem::path> runtime_files;
  bool session = false;
  bool importable = true;
};
struct Inspection {
  bool inputs_ok = true, prepared = false;
  std::vector<std::string> issues;
};
std::string Utf8(const std::filesystem::path& path);
std::vector<Target> ReadCatalog(const ProjectPaths& paths);
Inspection Inspect(const ProjectPaths& paths, const Target& target);
std::filesystem::path PreparationMarker(const ProjectPaths& paths, const Target& target);
// Validates the entire selected set before any write. Copies only verified missing
// files; existing different bytes are an error. Cancellation leaves valid copies
// reusable, never changes sources or user data. No arbitrary XEX execution.
size_t Import(const ProjectPaths& paths, const Target& target,
              const std::filesystem::path& source,
              const std::function<bool()>& cancelled = {});
}
