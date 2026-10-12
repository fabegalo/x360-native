#include "session_profile.h"
#include "sha256.h"
#include <stdexcept>
#include <fstream>
#include <toml++/toml.hpp>

namespace x360n {
SessionProfile LoadSessionProfile(const ProjectPaths& paths, const std::filesystem::path& file) {
  const auto manifest = std::filesystem::canonical(file);
  std::ifstream input(manifest, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open session profile: " + PathUtf8(manifest));
  auto table = toml::parse(input, PathUtf8(manifest));
  auto text = [&](const auto& node) {
    auto value = node.template value<std::string>();
    if (!value || value->empty()) throw std::runtime_error("missing session profile field: " + PathUtf8(manifest));
    return *value;
  };
  auto resolve = [&](const std::string& path) { return std::filesystem::weakly_canonical(manifest.parent_path() / PathFromUtf8(path)); };
  SessionProfile p;
  p.manifest = manifest;
  p.name = text(table["project"]["name"]);
  p.game_root = resolve(text(table["project"]["game_root"]));
  p.xex = resolve(text(table["entrypoint"]["file_path"]));
  p.exe = resolve(text(table["x360n"]["native_exe"]));
  p.user_root = UserDataRoot(paths, resolve(text(table["x360n"]["user_data_root"])));
  p.sha256 = text(table["x360n"]["xex_sha256"]);
  if (!IsSafeLabel(p.name)) throw std::runtime_error("invalid profile name");
  if (auto error = CheckLaunchable(paths, p.exe)) throw std::runtime_error(*error);
  auto digest = DigestFile(p.xex);
  if (!digest || digest->sha256 != p.sha256) throw std::runtime_error("XEX hash mismatch: " + PathUtf8(p.xex));
  if (!IsWithin(p.xex, p.game_root)) throw std::runtime_error("XEX outside profile game root");
  if (auto array = table["x360n"]["args"].as_array())
    for (const auto& value : *array) {
      auto arg = text(value);
      if (arg.starts_with("--game_data_root") || arg.starts_with("--user_data_root") ||
          arg.starts_with("--x360n_handoff") || arg.starts_with("--log_file") ||
          arg.starts_with("--parent_window"))
        throw std::runtime_error("reserved session argument: " + arg);
      p.args.push_back(arg);
    }
  if (auto array = table["x360n"]["launch_targets"].as_array())
    for (const auto& value : *array) p.launch_targets.push_back(resolve(text(value)));
  if (auto array = table["x360n"]["dashboard_return_flags"].as_array()) {
    p.dashboard_return_flags.clear();
    for (const auto& value : *array) {
      const auto flags = value.value<int64_t>();
      if (!flags || *flags < 0 || uint64_t(*flags) > UINT32_MAX)
        throw std::runtime_error("invalid dashboard_return_flags");
      p.dashboard_return_flags.push_back(uint32_t(*flags));
    }
  }
  return p;
}
bool MatchesLaunch(const SessionProfile& p, const LaunchRequest& request) {
  if (request.kind != LaunchRequest::Image || request.flags != 0) return false;
  std::error_code ec;
  const auto path = PathFromUtf8(request.path);
  if (!std::filesystem::equivalent(p.xex, path, ec) || ec) return false;
  auto digest = DigestFile(path);
  return digest && digest->sha256 == p.sha256;
}
}
