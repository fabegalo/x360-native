#include "session_profile.h"
#include "host_platform.h"
#include "sha256.h"
#include "preparation.h"
#include "digest_batch.h"
#include "../../titles/common/games_played.h"
#include <stdexcept>
#include <fstream>
#include <toml++/toml.hpp>

namespace x360n {
std::vector<std::string> SessionProfileArguments(const SessionProfile& profile, bool skip_boot_intro) {
  auto args = profile.args;
  if (skip_boot_intro) for (const auto& override : profile.skip_boot_intro_args) {
    const auto key = override.substr(0, override.find('=') + 1);
    bool replaced = false;
    for (auto& arg : args) if (arg.starts_with(key)) { arg = override; replaced = true; }
    if (!replaced) args.push_back(override);
  }
  return args;
}
SessionProfile LoadSessionProfile(const ProjectPaths& paths, const std::filesystem::path& file, bool validate_execution) {
  CheckVerificationCancellation();
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
  // Codegen may need a wider VFS root (an embedded system module); the title's
  // own content root for import and execution is then declared separately.
  if (table["x360n"]["game_data_root"]) p.game_root = resolve(text(table["x360n"]["game_data_root"]));
  p.xex = resolve(text(table["entrypoint"]["file_path"]));
  p.exe = ResolveNativeExecutable(paths, p.name, resolve(text(table["x360n"]["native_exe"])));
  p.user_root = UserDataRoot(paths, resolve(text(table["x360n"]["user_data_root"])));
  p.sha256 = text(table["x360n"]["xex_sha256"]);
  if (auto host = table["x360n"]["system_ui_host"].value<std::string>()) {
    const auto relative_host = PathFromUtf8(*host);
    if (relative_host.empty() || relative_host.has_parent_path() || relative_host.is_absolute())
      throw std::runtime_error("system UI host must belong to this dashboard build");
    p.system_ui_host = p.exe.parent_path() / NativeRuntimeFilename(relative_host);
    if (validate_execution && !std::filesystem::is_regular_file(p.system_ui_host))
      throw std::runtime_error("missing dashboard system UI host: " + PathUtf8(p.system_ui_host));
  }
  if (!IsSafeLabel(p.name)) throw std::runtime_error("invalid profile name");
  if (validate_execution) {
    if (auto error = CheckLaunchable(paths, p.exe)) throw std::runtime_error(*error);
    auto digest = DigestFile(p.xex);
    if (!digest || digest->sha256 != p.sha256) throw std::runtime_error("XEX hash mismatch: " + PathUtf8(p.xex));
    if (auto error = CheckPreparation(paths, p.name, manifest, p.exe)) throw std::runtime_error(*error);
  }
  const auto relative = p.xex.lexically_relative(p.game_root);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
      (validate_execution && !IsWithin(p.xex, p.game_root)))
    throw std::runtime_error("XEX outside profile game root");
  // Registration identity belongs to preparation.toml, shared with the importer.
  // Do not duplicate it in the AOT manifest or accept a hash at any arbitrary path.
  toml::table preparation;
  const auto setup = manifest.parent_path() / "preparation.toml";
  if (std::filesystem::is_regular_file(setup)) {
    std::ifstream registration(setup, std::ios::binary);
    preparation = toml::parse(registration, PathUtf8(setup));
  }
  if (!p.system_ui_host.empty()) {
    bool recorded = false;
    if (auto files = preparation["runtime_files"].as_array())
      for (const auto& value : *files)
        if (auto name=value.value<std::string>(); name && NativeRuntimeFilename(PathFromUtf8(*name))==p.system_ui_host.filename()) recorded = true;
    if (!recorded) throw std::runtime_error("system UI host is not owned by the dashboard preparation receipt");
  }
  if (auto version = preparation["dashboard_version"].value<std::string>()) {
    if (!ParseDashboardVersion(*version)) throw std::runtime_error("invalid dashboard_version: " + *version);
    p.dashboard_version = *version;
  }
  if (auto avatar = preparation["avatar"].as_table()) {
    // Only this dashboard's own registered components, never another version's.
    AvatarServiceFiles files;
    files.version = text((*avatar)["version"]);
    if (files.version != p.dashboard_version)
      throw std::runtime_error("avatar service version " + files.version +
                               " differs from dashboard_version '" + p.dashboard_version + "': " + PathUtf8(setup));
    auto component = [&](const char* key, AvatarServiceFiles::File& file) {
      const auto relative = text((*avatar)[key]["path"]);
      file.provenance = text((*avatar)[key]["provenance"]);
      // Lexical here: an absent file is reported at launch, not when loading.
      const auto normal = PathFromUtf8(relative).lexically_normal();
      if (normal.empty() || normal.has_root_path() || *normal.begin() == "..")
        throw std::runtime_error("avatar service file outside the dashboard game root: " + relative);
      file.path = std::filesystem::weakly_canonical(p.game_root / normal);
      if (auto registered = preparation["files"].as_array())
        for (const auto& entry : *registered)
          if (entry.as_table() && (*entry.as_table())["path"].value<std::string>() == relative)
            file.sha256 = text((*entry.as_table())["sha256"]);
      if (file.sha256.size() != 64 || file.sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("avatar service file has no registered SHA-256 in [[files]]: " + relative);
    };
    component("xam", files.xam);
    component("catalog", files.catalog);
    p.avatar_files = std::move(files);
  }
  if (auto version = table["x360n"]["avatar_service"].value<std::string>()) {
    if (!ParseDashboardVersion(*version)) throw std::runtime_error("invalid avatar_service: " + *version);
    p.avatar_service = *version;
    p.avatar_xam_sha256 = text(table["x360n"]["avatar_xam_sha256"]);
    if (p.avatar_xam_sha256.size() != 64 ||
        p.avatar_xam_sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("invalid avatar_xam_sha256: " + PathUtf8(manifest));
  }
  if (auto library = preparation["library"].as_table()) {
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
  auto read_args = [&](const char* key, std::vector<std::string>& destination) {
    if (auto array = table["x360n"][key].as_array())
    for (const auto& value : *array) {
      auto arg = text(value);
      if (arg.starts_with("--game_data_root") || arg.starts_with("--user_data_root") ||
          arg.starts_with("--content_data_root") ||
          arg.starts_with("--x360n_handoff") || arg.starts_with("--log_file") ||
          arg.starts_with("--x360n_profile_root") ||
          arg.starts_with("--x360n_dashboard_version") ||
          arg.starts_with("--x360n_system_ui_") ||
          arg.starts_with("--x360n_avatar_") ||
          arg.starts_with("--parent_window"))
        throw std::runtime_error("reserved session argument: " + arg);
      destination.push_back(arg);
    }
  };
  read_args("args", p.args);
  read_args("skip_boot_intro_args", p.skip_boot_intro_args);
  for (const auto& arg : p.skip_boot_intro_args)
    if (!arg.starts_with("--") || arg.find('=') == std::string::npos)
      throw std::runtime_error("skip_boot_intro_args requires --option=value: " + arg);
  if (auto array = table["x360n"]["launch_targets"].as_array())
    for (const auto& value : *array) p.launch_targets.push_back(resolve(text(value)));
  if (table["x360n"]["launch_library"].value_or(false)) {
    // Optional compatibility profiles and automatically generated local projects
    // share the same receipt-verified launch path. Metadata alone never launches.
    for (const auto& root : {paths.root / "titles", paths.local() / "aot"}) {
    if (!std::filesystem::is_directory(root)) continue;
    for (const auto& dir : std::filesystem::directory_iterator(root)) {
      CheckVerificationCancellation();
      try {
      const auto setup_file = dir.path() / "preparation.toml";
      if (!std::filesystem::is_regular_file(setup_file)) continue;
      std::ifstream in(setup_file); const auto recipe = toml::parse(in, PathUtf8(setup_file));
      if (!recipe["library"].is_table() || recipe["kind"].value_or(std::string("game")) != "game") continue;
      const auto id = PathUtf8(dir.path().filename());
      const auto candidate = dir.path() / PathFromUtf8(id + "_manifest.toml");
      // Generated projects declare their native output. It can use a compact
      // build directory while retaining the complete catalog/title identity.
      auto native = NativeTitleExecutable(paths, id);
      if (root == paths.local() / "aot") {
        std::ifstream manifest_input(candidate, std::ios::binary);
        const auto registration = toml::parse(manifest_input, PathUtf8(candidate));
        const auto configured = registration["x360n"]["native_exe"].value<std::string>();
        if (!configured || configured->empty()) continue;
        native = ResolveNativeExecutable(paths, id, std::filesystem::weakly_canonical(dir.path() / PathFromUtf8(*configured)));
      }
      if (root == paths.local() / "aot" &&
          (std::filesystem::exists(paths.local() / "preparation" / (id + ".runtime-error")) ||
           CheckPreparation(paths, id, candidate, native))) continue;
      if (std::filesystem::weakly_canonical(candidate) != manifest)
        p.launch_targets.push_back(candidate);
      } catch (const VerificationCancelled&) { throw; }
        catch (const std::exception&) {
        // An interrupted or malformed optional game project must not prevent
        // starting the original dashboard or another already verified title.
        continue;
      }
    }
    }
  }
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
std::vector<std::string> SessionPlatformArguments(const SessionProfile& dashboard, const SessionProfile& active) {
  std::vector<std::string> args{
      "--x360n_profile_root=" + PathUtf8(dashboard.user_root),
      "--content_data_root=" + PathUtf8(dashboard.user_root)};
  if (!dashboard.dashboard_version.empty())
    args.push_back("--x360n_dashboard_version=" + dashboard.dashboard_version);
  if (active.manifest != dashboard.manifest && !dashboard.system_ui_host.empty()) {
    args.push_back("--x360n_system_ui_host=" + PathUtf8(dashboard.system_ui_host));
    args.push_back("--x360n_system_ui_root=" + PathUtf8(dashboard.game_root));
  }
  for (auto& arg : AvatarServiceArguments(dashboard, active)) args.push_back(std::move(arg));
  return args;
}
std::vector<std::string> AvatarServiceArguments(const SessionProfile& dashboard, const SessionProfile& title) {
  if (title.avatar_service.empty()) return {};
  const auto& version = title.avatar_service;
  const auto service = "avatar service " + version + " of " + title.name + ": ";
  if (dashboard.dashboard_version != version)
    throw std::runtime_error(service + "requires the session dashboard " + version + "; " + dashboard.name +
        (dashboard.dashboard_version.empty() ? " has no dashboard version" : " is " + dashboard.dashboard_version) +
        ". Start the title from the dashboard " + version + " session.");
  if (!dashboard.avatar_files)
    throw std::runtime_error(service + "dashboard " + dashboard.name + " (" + version +
        ") registers no avatar files (preparation.toml [avatar]).");
  // Its system UI host binds the one XAM image the service and the Guide share.
  if (dashboard.system_ui_host.empty())
    throw std::runtime_error(service + "dashboard " + dashboard.name + " (" + version +
        ") has no system UI host, which provides the shared XAM image.");
  const auto& files = *dashboard.avatar_files;
  if (files.xam.sha256 != title.avatar_xam_sha256)
    throw std::runtime_error(service + "compiled against xam.xex SHA-256 " + title.avatar_xam_sha256 +
        ", but dashboard " + dashboard.name + " registers " + PathUtf8(files.xam.path.filename()) +
        " SHA-256 " + files.xam.sha256 + ".");
  for (const auto* file : {&files.xam, &files.catalog}) {
    const auto identity = PathUtf8(file->path.filename()) + " " + version + " (SHA-256 " + file->sha256 +
                          "; " + file->provenance + ")";
    if (!std::filesystem::is_regular_file(file->path))
      throw std::runtime_error(service + "missing " + identity + ". Import it into the dashboard " + version +
                               ": " + PathUtf8(file->path));
    if (!IsWithin(file->path, dashboard.game_root))
      throw std::runtime_error(service + identity + " resolves outside the dashboard game root: " +
                               PathUtf8(file->path));
    const auto digest = DigestFile(file->path);
    if (!digest || digest->sha256 != file->sha256)
      throw std::runtime_error(service + "different file instead of " + identity +
          (digest ? ", found SHA-256 " + digest->sha256 : std::string(", unreadable")) +
          ". It is preserved; import the " + version + " original: " + PathUtf8(file->path));
  }
  // The title's cvars carry the build number of the version it was built against.
  const auto build = std::to_string((*ParseDashboardVersion(version))[2]);
  return {"--x360n_avatar_xam_" + build + "=" + PathUtf8(files.xam.path),
          "--x360n_avatar_catalog_" + build + "=" + PathUtf8(files.catalog.path)};
}
}
