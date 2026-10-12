#pragma once
#include "paths.h"
#include "../../titles/common/launch_request.h"
#include <vector>
namespace x360n {
struct SessionProfile {
  std::filesystem::path manifest, exe, xex, game_root, user_root;
  std::string name, sha256;
  std::vector<std::string> args;
  std::vector<std::filesystem::path> launch_targets;
};
SessionProfile LoadSessionProfile(const ProjectPaths& paths, const std::filesystem::path& manifest);
bool MatchesLaunch(const SessionProfile& profile, const LaunchRequest& request);
}
