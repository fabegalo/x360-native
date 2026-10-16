#pragma once
#include "paths.h"
#include "../../titles/common/launch_request.h"
#include <optional>
#include <vector>
namespace x360n {
// Original files of a dashboard's avatar service (preparation.toml [avatar]).
// Hashes come from that receipt's [[files]]; nothing is shared across versions.
struct AvatarServiceFiles {
  struct File {
    std::filesystem::path path;
    std::string sha256, provenance;
  };
  std::string version;  // equals the dashboard_version of the same receipt
  File xam, catalog;
};
struct SessionProfile {
  std::filesystem::path manifest, exe, xex, game_root, user_root;
  std::filesystem::path library_relative_xex;
  std::filesystem::path system_ui_host;
  std::string dashboard_version;  // preparation.toml of a dashboard, e.g. 2.0.17559.0
  std::string name, sha256;
  std::vector<std::string> args;
  std::vector<std::filesystem::path> launch_targets;
  std::vector<uint32_t> dashboard_return_flags{0};
  std::optional<AvatarServiceFiles> avatar_files;  // dashboard: what it provides
  // Title: dashboard version and exact xam.xex its avatar service was compiled
  // against (manifest [x360n] avatar_service, avatar_xam_sha256).
  std::string avatar_service, avatar_xam_sha256;
};
// Catalog reads retain routing metadata; execution always validates the files.
SessionProfile LoadSessionProfile(const ProjectPaths& paths, const std::filesystem::path& manifest,
                                  bool validate_execution = true);
bool MatchesLaunch(const SessionProfile& profile, const LaunchRequest& request,
                   const std::filesystem::path& dashboard_user_root = {});
// Arguments that hand the session dashboard's verified avatar files to a title
// that declares avatar_service; none for other titles. Throws before the title
// starts when the version, registration or bytes are absent or differ.
std::vector<std::string> AvatarServiceArguments(const SessionProfile& dashboard,
                                                const SessionProfile& title);
}
