#pragma once
#include "paths.h"
#include <functional>
#include <vector>

namespace x360n::desktop {
struct LibraryContent {
  std::string title_id, content_type, file_name, display_name, source_sha256;
};
struct Input {
  std::filesystem::path destination;
  std::string source_name, sha256;
  std::filesystem::path source_relative;
  bool optional = false;  // may be absent; when present it must match
};
struct Target {
  std::string id, label, limitations, label_en, limitations_en;
  std::string kind = "game";
  std::filesystem::path manifest, game_root, user_root, exe;
  std::filesystem::path preparation_source;
  std::vector<Input> inputs;
  std::vector<std::string> dependencies;
  std::vector<std::filesystem::path> runtime_files;
  LibraryContent library;
  bool session = false;
  bool importable = true;
  bool aot = true, catalogued = false, generic_aot = false;
  std::string title_id, version, media_id, original_name, import_error;
};
struct Inspection {
  bool inputs_ok = true, prepared = false, interrupted = false;
  std::vector<std::string> issues;
};
struct ContentIdentity {
  std::filesystem::path file;
  std::string target_id, label, version, kind, problem;
  bool recognized = false;
  bool valid = false;
  std::string sha256, title_id, media_id, original_name;
  std::string base_version, region;
  unsigned disc_number = 0, disc_count = 0;
  std::vector<std::string> imports;
};
// Read-only inspection; recognizing a format is never a compatibility claim.
std::vector<ContentIdentity> IdentifyContent(const std::vector<Target>& targets,
    const std::filesystem::path& source, const std::function<bool()>& cancelled = {});
std::string Utf8(const std::filesystem::path& path);
std::vector<Target> ReadCatalog(const ProjectPaths& paths);
// Durable local game inventory, independent of the AOT recipe catalog.
void ReadImportedGames(const ProjectPaths& paths, std::vector<Target>& targets);
// Generate a deterministic local project from verified content and the pinned SDK.
void GenerateAotProject(const ProjectPaths& paths, const Target& target);
std::string ImportGame(const ProjectPaths& paths, const std::vector<Target>& targets,
    const std::filesystem::path& source, const std::filesystem::path& original,
    const std::function<bool()>& cancelled = {});
std::vector<std::string> DetectImportTargets(const std::vector<Target>& targets,
                                            const std::filesystem::path& source,
                                            const std::function<bool()>& cancelled = {});
Inspection Inspect(const ProjectPaths& paths, const Target& target);
// Installs verified dependencies into the selected dashboard's SDK content store.
// The original dashboard still enumerates and launches them through XAM.
std::vector<std::string> InspectLibrary(const ProjectPaths& paths, const Target& session, const std::vector<Target>& catalog);
void PrepareLibrary(const ProjectPaths& paths, const Target& session, const std::vector<Target>& catalog,
                    const std::function<bool()>& cancelled = {});
std::filesystem::path PreparationMarker(const ProjectPaths& paths, const Target& target);
// Validates the entire selected set before any write. Copies only verified missing
// files; existing different bytes are an error. Cancellation leaves valid copies
// reusable, never changes sources or user data. No arbitrary XEX execution.
size_t Import(const ProjectPaths& paths, const Target& target,
              const std::filesystem::path& source,
              const std::function<bool()>& cancelled = {}, bool allow_partial = false);
struct PayloadStatus {
  bool current = false, interrupted = false;
  std::vector<std::string> components;
};
PayloadStatus InspectEnvironment(const std::filesystem::path& payload,
                                const std::filesystem::path& destination);
void VerifyPackage(const std::filesystem::path& directory, const std::string& expected_payload = {});
// Replacement requires explicit consent; originals remain in local backups.
void InitializeEnvironment(const std::filesystem::path& payload,
                           const std::filesystem::path& destination,
                           bool allow_update = false,
                           const std::function<void()>& checkpoint = {});
}
