// Explicit, bounded diagnostic only. Never registered as an automatic test.
// <root> <title> <output> <isolated-user> <dashboard> [seconds] [--probe-storage] [--xenos-d3d12] [--debug-log]
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
#include <iomanip>
using namespace x360n;
using namespace x360n::desktop;
int main(int argc, char** argv) {
  if (argc < 6 || argc > 11) return 2;
  namespace fs = std::filesystem;
  try {
    const ProjectPaths paths{fs::absolute(PathFromUtf8(argv[1]))};
    const std::string id = argv[2];
    const auto output = fs::absolute(PathFromUtf8(argv[3]));
    const auto user = fs::absolute(PathFromUtf8(argv[4]));
    const std::string dashboard_id = argv[5];
    const auto seconds = argc >= 7 ? std::stoi(argv[6]) : 30;
    bool storage_probe = false, xenos_d3d12 = false, debug_log = false;
    uint64_t crash_memory_base = 0;
    for (int i = 7; i < argc; ++i) {
      if (std::string(argv[i]) == "--probe-storage") storage_probe = true;
      else if (std::string(argv[i]) == "--xenos-d3d12") xenos_d3d12 = true;
      else if (std::string(argv[i]) == "--debug-log") debug_log = true;
      else if (std::string(argv[i]).starts_with("--crash-memory-base="))
        crash_memory_base = std::stoull(std::string(argv[i]).substr(20),nullptr,16);
      else return 2;
    }
    if (seconds < 1 || seconds > 180) throw std::runtime_error("Diagnostic duration must be 1..180 seconds");
    if (fs::exists(output) || !fs::is_directory(user)) return 2;
    if (!IsWithin(user, paths.local() / "evidence") || !IsWithin(output.parent_path(), paths.local() / "evidence") ||
        IsWithin(output.parent_path(), user))
      throw std::runtime_error("Smoke requires separate evidence and isolated user-copy directories");
    for (const auto& protected_root : {paths.inputs(), paths.build(), paths.root / "titles", paths.sdk()})
      if (IsWithin(user, protected_root)) throw std::runtime_error("Smoke user directory is not isolated");
    fs::create_directories(output);
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(); };
    // Register only this session's closure in its disposable HDD. Keep the
    // normal My Games verification, without writing to real console storage
    // or requiring copies in unrelated dashboard versions.
    const auto available = ReadCatalog(paths);
    std::vector<Target> catalog;
    auto include = [&](const std::string& name) {
      for (const auto& target : catalog) if (target.id == name) return;
      for (const auto& target : available) if (target.id == name) {
        auto copy = target;
        copy.user_root = user / target.user_root.filename();
        catalog.push_back(std::move(copy));
        return;
      }
      throw std::runtime_error("Missing target: " + name);
    };
    include(id); include(dashboard_id);
    for (size_t i = 0; i < catalog.size(); ++i) {
      const auto dependencies = catalog[i].dependencies;
      for (const auto& dependency : dependencies) include(dependency);
    }
    const auto find = [&](const std::string& name) -> const Target& {
      for (const auto& target : catalog) if (target.id == name) return target;
      throw std::runtime_error("Missing target: " + name);
    };
    const auto& console = find(dashboard_id);
    bool registration_present = true;
    for (const auto& game : catalog) if (InGameLibrary(game) && !game.library.title_id.empty()) {
      const auto header = console.user_root / "0000000000000000" / game.library.title_id /
          "Headers" / game.library.content_type / (game.library.file_name + ".header");
      registration_present &= fs::is_regular_file(header);
    }
    // This hint only skips redundant writes. The complete launch gate below
    // still hashes the registered header, payload, originals and receipts.
    if (!registration_present) PrepareLibrary(paths, console, catalog);
    WorkspaceLease lease(paths);
    auto batch = std::make_unique<DigestBatch>();
    CatalogLaunchVerification launch_paths;
    PrimeLaunchVerification(paths, catalog, id);
    const auto selected = VerifyCatalogLaunch(paths, catalog, id, find(id).manifest);
    const auto title = LoadSessionProfile(paths, selected.manifest);
    auto dashboard = id == dashboard_id ? title : LoadSessionProfile(paths, find(dashboard_id).manifest);
    for (const auto& source : {title.user_root, dashboard.user_root})
      if (IsWithin(user, source) || IsWithin(source, user))
        throw std::runtime_error("Smoke must not use original user storage");
    const auto renderer = xenos_d3d12 ? RendererSettings{} : LoadRendererSettings(paths);
    CheckSessionRenderer(title.exe, renderer);
    auto args = SessionConsoleArguments(SessionRendererArguments(SessionProfileArguments(title, false), renderer), LoadConsoleSettings(paths));
    args.push_back("--game_data_root=" + PathUtf8(title.game_root));
    args.push_back("--user_data_root=" + PathUtf8(user / title.user_root.filename()));
    args.push_back("--cache_root=" + PathUtf8(user / title.user_root.filename() / "cache"));
    dashboard.user_root = user / dashboard.user_root.filename();
    for (auto& arg : SessionPlatformArguments(dashboard, title)) args.push_back(std::move(arg));
    if (storage_probe) {
      // This diagnostic belongs to the exact original system host, never to
      // a game. It submits XAM's request; the user still owns the selection.
      if (dashboard.dashboard_version != "2.0.17559.0" || dashboard.system_ui_host.empty())
        throw std::runtime_error("Storage service probe requires the verified 17559 host");
      args.push_back("--dash17559_selector_probe_seconds=5");
    }
    args.push_back("--log_file=" + PathUtf8(output / "rex.log"));
    if (debug_log) args.push_back("--log_level=debug");
    if (selected.generic_aot)
      args.push_back("--x360n_guest_fault_context=" + PathUtf8(output / "guest-fault.txt"));
    args.push_back("--perf_counters=true");
    args.push_back("--perf_log_csv=" + PathUtf8(output / "startup.csv"));
    if (auto error = CheckPreparedLaunch(paths, title.exe)) throw std::runtime_error(*error);
    if (auto error = CheckLaunchable(paths, title.exe)) throw std::runtime_error(*error);
    const auto title_digest = DigestFile(title.exe);
    if (!title_digest) throw std::runtime_error("Unreadable executable");
    for (const auto& file : fs::directory_iterator(title.exe.parent_path())) {
      auto ext = PathUtf8(file.path().extension());
      for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
      if (ext == ".dll" && !DigestFile(file.path())) throw std::runtime_error("Unreadable DLL");
    }
    const auto files = batch->files, bytes = batch->bytes;
    fs::copy_file(PreparationReceipt(title.exe), output / "title.preparation.json");
    fs::copy_file(PreparationReceipt(dashboard.exe), output / "dashboard.preparation.json");
    batch.reset();
    const auto preflight_ms = elapsed();
    std::cout << "Verified console diagnostic: " << id << " via " << dashboard_id
              << "; child limit=" << seconds << " seconds; output=" << PathUtf8(output) << std::endl;
    ProcessSpec spec;
    spec.exe = title.exe; spec.args = args; spec.working_dir = title.exe.parent_path();
    spec.stdout_file = output / "stdout.log"; spec.stderr_file = output / "stderr.log";
    spec.timeout = std::chrono::seconds(seconds); spec.no_console = true;
    spec.capture_crash = true;
    spec.crash_guest_memory_base = crash_memory_base;
    int64_t process_ms = -1;
    spec.on_started = [&] { process_ms = elapsed(); };
    const auto result = RunProcess(spec);
    JsonWriter out;
    out.BeginObject().String("title", id).String("dashboard", dashboard_id)
        .String("exe_sha256", title_digest->sha256)
        .Number("preflight_ms", preflight_ms).Number("process_started_ms", process_ms)
        .Number("child_ms", result.duration.count()).Number("exit_code", result.exit_code)
        .Bool("launched", result.launched).Bool("timed_out", result.timed_out)
        .Bool("storage_probe", storage_probe).Bool("crash_captured", result.crash.captured)
        .Bool("xenos_d3d12_override", xenos_d3d12)
        .Bool("debug_log", debug_log).Bool("crash_memory_capture", crash_memory_base != 0)
        .Number("crash_code", result.crash.code).Number("crash_address", result.crash.address)
        .String("crash_access", result.crash.access)
        .Number("sha_files", files).Number("sha_bytes", bytes).String("error", result.launch_error)
        .String("limit", "Direct supervised smoke with isolated user copies; no My Games navigation or visual certification")
        .EndObject();
    const auto json = out.Take();
    std::ofstream(output / "result.json") << json;
    if (result.crash.captured) {
      std::ofstream stack(output / "crash.txt");
      for (const auto& frame : result.crash.frames) stack << frame << '\n';
      if (crash_memory_base) {
        std::ofstream memory(output / "crash-memory.txt");
        memory << std::hex << std::uppercase << std::setfill('0');
        for (const auto& [name,value] : result.crash.native_registers)
          memory << name << '=' << std::setw(16) << value << '\n';
        for (const auto& block : result.crash.memory) {
          memory << "memory[" << std::setw(16) << block.address << "]=";
          for (auto byte : block.bytes) memory << std::setw(2) << unsigned(byte);
          memory << '\n';
        }
      }
    }
    std::cout << json;
    return result.launched && (result.timed_out || result.exit_code == 0) ? 0 : 1;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
