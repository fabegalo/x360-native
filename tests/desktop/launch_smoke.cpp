// Explicit, bounded diagnostic only. Never registered as an automatic test.
#include "catalog.h"
#include "launch_preflight.h"
#include "digest_batch.h"
#include "session_profile.h"
#include "renderer_settings.h"
#include "console_settings.h"
#include "preparation.h"
#include "child_process.h"
#include "report.h"
#include "workspace_lease.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <cctype>
#include <memory>
using namespace x360n;
using namespace x360n::desktop;
int main(int argc, char** argv) {
  if (argc != 6) return 2;
  namespace fs = std::filesystem;
  try {
    const ProjectPaths paths{fs::absolute(PathFromUtf8(argv[1]))};
    const std::string id = argv[2];
    const auto output = fs::absolute(PathFromUtf8(argv[3]));
    const auto user = fs::absolute(PathFromUtf8(argv[4]));
    const std::string dashboard_id = argv[5];
    if (fs::exists(output) || !fs::is_directory(user)) return 2;
    if (!IsWithin(user, paths.local() / "evidence") || !IsWithin(output.parent_path(), paths.local() / "evidence") ||
        IsWithin(output.parent_path(), user))
      throw std::runtime_error("Smoke requires separate evidence and isolated user-copy directories");
    for (const auto& protected_root : {paths.inputs(), paths.build(), paths.root / "titles", paths.sdk()})
      if (IsWithin(user, protected_root)) throw std::runtime_error("Smoke user directory is not isolated");
    fs::create_directories(output);
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(); };
    WorkspaceLease lease(paths);
    auto batch = std::make_unique<DigestBatch>();
    CatalogLaunchVerification launch_paths;
    const auto catalog = ReadCatalog(paths);
    const auto find = [&](const std::string& name) -> const Target& {
      for (const auto& target : catalog) if (target.id == name) return target;
      throw std::runtime_error("Missing target: " + name);
    };
    PrimeLaunchVerification(paths, catalog, id);
    const auto selected = VerifyCatalogLaunch(paths, catalog, id, find(id).manifest);
    const auto title = LoadSessionProfile(paths, selected.manifest);
    const auto dashboard = id == dashboard_id ? title : LoadSessionProfile(paths, find(dashboard_id).manifest);
    for (const auto& source : {title.user_root, dashboard.user_root})
      if (IsWithin(user, source) || IsWithin(source, user))
        throw std::runtime_error("Smoke must not use original user storage");
    const auto renderer = LoadRendererSettings(paths);
    CheckSessionRenderer(title.exe, renderer);
    auto args = SessionConsoleArguments(SessionRendererArguments(SessionProfileArguments(title, false), renderer), LoadConsoleSettings(paths));
    args.push_back("--game_data_root=" + PathUtf8(title.game_root));
    args.push_back("--user_data_root=" + PathUtf8(user / title.user_root.filename()));
    args.push_back("--cache_root=" + PathUtf8(user / title.user_root.filename() / "cache"));
    args.push_back("--x360n_profile_root=" + PathUtf8(user / dashboard.user_root.filename()));
    if (!dashboard.dashboard_version.empty()) args.push_back("--x360n_dashboard_version=" + dashboard.dashboard_version);
    if (title.manifest != dashboard.manifest && !dashboard.system_ui_host.empty()) {
      args.push_back("--x360n_system_ui_host=" + PathUtf8(dashboard.system_ui_host));
      args.push_back("--x360n_system_ui_root=" + PathUtf8(dashboard.game_root));
    }
    for (auto& arg : AvatarServiceArguments(dashboard, title)) args.push_back(std::move(arg));
    args.push_back("--log_file=" + PathUtf8(output / "rex.log"));
    args.push_back("--perf_counters=true");
    args.push_back("--perf_log_csv=" + PathUtf8(output / "startup.csv"));
    if (auto error = CheckPreparedLaunch(paths, title.exe)) throw std::runtime_error(*error);
    if (auto error = CheckLaunchable(paths, title.exe)) throw std::runtime_error(*error);
    if (!DigestFile(title.exe)) throw std::runtime_error("Unreadable executable");
    for (const auto& file : fs::directory_iterator(title.exe.parent_path())) {
      auto ext = PathUtf8(file.path().extension());
      for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
      if (ext == ".dll" && !DigestFile(file.path())) throw std::runtime_error("Unreadable DLL");
    }
    const auto files = batch->files, bytes = batch->bytes;
    batch.reset();
    const auto preflight_ms = elapsed();
    ProcessSpec spec;
    spec.exe = title.exe; spec.args = args; spec.working_dir = title.exe.parent_path();
    spec.stdout_file = output / "stdout.log"; spec.stderr_file = output / "stderr.log";
    spec.timeout = std::chrono::seconds(30); spec.no_console = true;
    int64_t process_ms = -1;
    spec.on_started = [&] { process_ms = elapsed(); };
    const auto result = RunProcess(spec);
    JsonWriter out;
    out.BeginObject().String("title", id).String("dashboard", dashboard_id)
        .Number("preflight_ms", preflight_ms).Number("process_started_ms", process_ms)
        .Number("child_ms", result.duration.count()).Number("exit_code", result.exit_code)
        .Bool("launched", result.launched).Bool("timed_out", result.timed_out)
        .Number("sha_files", files).Number("sha_bytes", bytes).String("error", result.launch_error)
        .String("limit", "Direct supervised smoke with isolated user copies; no My Games navigation or visual certification")
        .EndObject();
    const auto json = out.Take();
    std::ofstream(output / "result.json") << json;
    std::cout << json;
    return result.launched ? 0 : 1;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
