#pragma once
#include "paths.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace x360n { class ReceiptHints; enum class ReceiptHint; }
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
  bool launch_library = false;  // explicit manifest opt-in, independent of opening a dashboard
  bool importable = true;
  bool aot = true, catalogued = false, generic_aot = false;
  std::string title_id, version, media_id, original_name, import_error;
  std::string catalog_id;  // full content identity of the imported record, if any
  // Package icon recorded at import (content-addressed in .local/catalog/thumbnails).
  std::filesystem::path thumbnail;
  std::string thumbnail_sha256;
};
struct Inspection {
  bool inputs_ok = true, prepared = false, interrupted = false;
  // Presentation metadata never authenticates content or grants launchability.
  bool verification_pending = false, launch_candidate = false;
  // Preview hint from file times; never a substitute for the full check.
  bool receipt_current = false, receipt_stale = false;
  std::vector<std::string> issues;
};
enum class InspectionMode { Full, Preview };
struct ContentIdentity {
  std::filesystem::path file;
  std::string target_id, label, version, kind, problem;
  bool recognized = false;
  bool valid = false;
  std::string sha256, title_id, media_id, original_name;
  std::string base_version, region;
  unsigned disc_number = 0, disc_count = 0;
  uint32_t module_flags = 0, entry_point = 0, image_base = 0, image_size = 0;
  bool resource_only = false;
  std::vector<std::string> imports;
};
// Read-only inspection; recognizing a format is never a compatibility claim.
std::vector<ContentIdentity> IdentifyContent(const std::vector<Target>& targets,
    const std::filesystem::path& source, const std::function<bool()>& cancelled = {});
std::string Utf8(const std::filesystem::path& path);
// Enables protected path reuse only for the launch operation. Ordinary catalog
// opening, imports and preparation retain their existing resolution path.
class CatalogLaunchVerification {
 public:
  CatalogLaunchVerification();
  ~CatalogLaunchVerification();
  CatalogLaunchVerification(const CatalogLaunchVerification&) = delete;
  CatalogLaunchVerification& operator=(const CatalogLaunchVerification&) = delete;
 private:
  bool previous_;
};
bool CatalogLaunchVerificationActive();
std::filesystem::path CatalogVerificationPath(const std::filesystem::path& path);
std::vector<Target> ReadCatalog(const ProjectPaths& paths);
// Durable local game inventory, independent of the AOT recipe catalog.
void ReadImportedGames(const ProjectPaths& paths, std::vector<Target>& targets);
// Generate a deterministic local project from verified content and the pinned SDK.
void GenerateAotProject(const ProjectPaths& paths, const Target& target);
std::string ImportGame(const ProjectPaths& paths, const std::vector<Target>& targets,
    const std::filesystem::path& source, const std::filesystem::path& original,
    const std::function<bool()>& cancelled = {});
// Presentation only: the recorded icon bytes, or empty when absent or changed.
std::vector<uint8_t> ReadThumbnail(const Target& target);
std::vector<std::string> DetectImportTargets(const std::vector<Target>& targets,
                                            const std::filesystem::path& source,
                                            const std::function<bool()>& cancelled = {});
// Records a listing hint on a Preview inspection (stale withdraws the candidate).
void ApplyReceiptHint(Inspection& state, ReceiptHint hint);
// Preview never hashes. With `hints`, a built title's receipt is compared by
// file times (presentation only); sharing one instance examines each file once.
Inspection Inspect(const ProjectPaths& paths, const Target& target, InspectionMode mode = InspectionMode::Full,
                   ReceiptHints* hints = nullptr);
// Complete launch preflight. Caller keeps its DigestBatch alive through the
// supervisor's remaining checks; metadata alone never authorizes execution.
Target VerifyCatalogLaunch(const ProjectPaths& paths, const std::vector<Target>& catalog,
    const std::string& id, const std::filesystem::path& manifest);
// Installs verified dependencies into the selected dashboard's SDK content store.
// The original dashboard still enumerates and launches them through XAM.
std::vector<std::string> InspectLibrary(const ProjectPaths& paths, const Target& session, const std::vector<Target>& catalog);
void PrepareLibrary(const ProjectPaths& paths, const Target& session, const std::vector<Target>& catalog,
                    const std::function<bool()>& cancelled = {});
// Withdraws a game's registration and verified copy from one dashboard's store.
// Unidentified data fails closed before any change. The caller holds the lease.
void WithdrawLibrary(const Target& session, const Target& game, const std::vector<Target>& catalog);
// Removes an imported game from the library: its My Games registrations, the
// environment's imported copy and its generated AOT project/build, then the
// catalog record (last, so an interrupted removal can be retried). Saves,
// profiles and the original source are kept; a folder adopted in place is not
// deleted. Never applies to dashboards or catalogue-only recipes.
void RemoveGame(const ProjectPaths& paths, const std::vector<Target>& catalog, const std::string& id);
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
