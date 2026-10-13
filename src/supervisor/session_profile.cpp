#include "session_profile.h"
#include "sha256.h"
#include <stdexcept>
#include <fstream>
#include <toml++/toml.hpp>

namespace x360n {
SessionProfile LoadSessionProfile(const ProjectPaths& paths, const std::filesystem::path& file, bool validate_execution) {
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
  if (validate_execution) {
    if (auto error = CheckLaunchable(paths, p.exe)) throw std::runtime_error(*error);
    auto digest = DigestFile(p.xex);
    if (!digest || digest->sha256 != p.sha256) throw std::runtime_error("XEX hash mismatch: " + PathUtf8(p.xex));
  }
  const auto relative = p.xex.lexically_relative(p.game_root);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
      (validate_execution && !IsWithin(p.xex, p.game_root)))
    throw std::runtime_error("XEX outside profile game root");
  if (auto library = table["library"].as_table()) {
    const auto title = text((*library)["title_id"]);
    const auto type = text((*library)["content_type"]);
    const auto name = text((*library)["file_name"]);
    for (const auto& id : {title, type})
      if (id.size() != 8 || id.find_first_not_of("0123456789ABCDEF") != std::string::npos)
        throw std::runtime_error("invalid library identity");
    if (!IsSafePackageEntryName(name) || name.size() > 42)
      throw std::runtime_error("invalid library content name");
    p.library_relative_xex = std::filesystem::path("0000000000000000") / title / type / name / relative;
  }
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
bool MatchesLaunch(const SessionProfile& p, const LaunchRequest& request,
                   const std::filesystem::path& dashboard_user_root) {
  if (request.kind != LaunchRequest::Image || request.flags != 0) return false;
  auto native = [](const std::filesystem::path& file) -> std::filesystem::path {
#ifdef _WIN32
    auto name = std::filesystem::absolute(file).lexically_normal().make_preferred().native();
    if (name.starts_with(L"\\\\?\\")) return name;
    if (name.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + name.substr(2);
    return L"\\\\?\\" + name;
#else
    return file;
#endif
  };
  std::error_code ec;
  const auto path = native(PathFromUtf8(request.path));
  bool matches = std::filesystem::equivalent(native(p.xex), path, ec) && !ec;
  // Normal library installs are verified copies, not junctions to the import.
  // Permit only this profile's registered content address in this dashboard.
  if (!matches && !dashboard_user_root.empty() && !p.library_relative_xex.empty()) {
    ec.clear();
    matches = std::filesystem::equivalent(native(dashboard_user_root / p.library_relative_xex), path, ec) && !ec;
  }
  if (!matches) return false;
  auto digest = DigestFile(path);
  return digest && digest->sha256 == p.sha256;
}
}
