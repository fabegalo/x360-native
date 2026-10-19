// Read-only launch-path benchmark: never creates a title process or a window.
#include "catalog.h"
#include "digest_batch.h"
#include "preparation.h"
#include "session_profile.h"
#include "renderer_settings.h"
#include "report.h"
#include "launch_preflight.h"
#include <chrono>
#include <iostream>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
using namespace x360n;
using namespace x360n::desktop;
#ifdef _WIN32
uint64_t CpuTicks() {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
  return ((uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
         ((uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime);
}
#endif
int main(int argc, char** argv) {
  if (argc != 4) return 2;
  try {
    const ProjectPaths paths{std::filesystem::absolute(argv[1])};
    const std::string id = argv[2], mode = argv[3];
    if (mode != "split" && mode != "shared") return 2;
    auto start = std::chrono::steady_clock::now();
#ifdef _WIN32
    auto cpu_start = CpuTicks();
    IO_COUNTERS io_start{}; GetProcessIoCounters(GetCurrentProcess(), &io_start);
#endif
    auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
    uint64_t files = 0, bytes = 0, hits = 0;
    uint64_t last_files = 0, last_bytes = 0, last_hits = 0;
    auto batch = std::make_unique<DigestBatch>();
    JsonWriter stages;
    stages.BeginObject().BeginArray("stages");
    double last_ms = 0;
#ifdef _WIN32
    IO_COUNTERS last_io = io_start;
#endif
    auto stage = [&](const char* name) {
      const auto now = elapsed();
      stages.BeginObject().String("name", name).Number("ms", int64_t(now - last_ms));
      const auto current_files = files + (batch ? batch->files : 0);
      const auto current_bytes = bytes + (batch ? batch->bytes : 0);
      const auto current_hits = hits + (batch ? batch->hits : 0);
      stages.Number("sha_files", int64_t(current_files - last_files))
          .Number("sha_bytes", int64_t(current_bytes - last_bytes))
          .Number("cache_hits", int64_t(current_hits - last_hits));
      last_files = current_files; last_bytes = current_bytes; last_hits = current_hits;
#ifdef _WIN32
      IO_COUNTERS io{}; GetProcessIoCounters(GetCurrentProcess(), &io);
      stages.Number("read_bytes", int64_t(io.ReadTransferCount - last_io.ReadTransferCount))
          .Number("read_operations", int64_t(io.ReadOperationCount - last_io.ReadOperationCount))
          .Number("other_operations", int64_t(io.OtherOperationCount - last_io.OtherOperationCount));
      last_io = io;
#endif
      stages.EndObject(); last_ms = now;
    };
    std::unique_ptr<CatalogLaunchVerification> launch_paths;
    if (mode == "shared") launch_paths = std::make_unique<CatalogLaunchVerification>();
    auto record = [&] { files += batch->files; bytes += batch->bytes; hits += batch->hits; };
    const auto catalog = ReadCatalog(paths);
    const auto catalog_ms = elapsed();
    stage("catalog");
    if (mode == "split") {
      // The old desktop already has its catalog from opening. Charge the fresh
      // supervisor read only to the shared path, as in the actual launch flow.
      batch.reset(); batch = std::make_unique<DigestBatch>();
      start = std::chrono::steady_clock::now();
#ifdef _WIN32
      cpu_start = CpuTicks(); GetProcessIoCounters(GetCurrentProcess(), &io_start);
      last_io = io_start;
#endif
      last_ms = 0; last_files = 0; last_bytes = 0; last_hits = 0;
    }
    const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const auto& t) { return t.id == id; });
    if (found == catalog.end()) throw std::runtime_error("target missing");
    PrimeLaunchVerification(paths, catalog, id);
    stage("read_ahead");
    const auto selected = VerifyCatalogLaunch(paths, catalog, id, found->manifest);
    const auto preflight_ms = elapsed();
    stage("catalog_validation");
    if (mode == "split") { record(); batch.reset(); batch = std::make_unique<DigestBatch>(); }
    auto profile = LoadSessionProfile(paths, selected.manifest);
    BindLibraryLaunchTargets(profile, catalog);
    stage("session_profile");
    CheckSessionRenderer(profile.exe, LoadRendererSettings(paths));
    for (const auto& target : profile.launch_targets) LoadSessionProfile(paths, target, false);
    stage("session_dependencies");
    if (auto error = CheckPreparedLaunch(paths, profile.exe)) throw std::runtime_error(*error);
    if (auto error = CheckLaunchable(paths, profile.exe)) throw std::runtime_error(*error);
    stage("prepared_launch");
    if (!DigestFile(profile.exe)) throw std::runtime_error("executable unreadable");
    for (const auto& file : std::filesystem::directory_iterator(profile.exe.parent_path()))
      if (file.path().extension() == ".dll" && !DigestFile(file.path())) throw std::runtime_error("DLL unreadable");
    record(); batch.reset();
    stage("process_identity_and_release");
    stages.EndArray().EndObject();
    JsonWriter out;
    out.BeginObject().String("title", id).String("mode", mode)
       .Number("library_launch_targets", int64_t(profile.launch_targets.size()))
       .Number("catalog_ms", int64_t(catalog_ms))
       .Number("preflight_ms", int64_t(preflight_ms)).Number("total_ms", int64_t(elapsed()))
       .Number("files", int64_t(files)).Number("bytes", int64_t(bytes)).Number("hits", int64_t(hits));
#ifdef _WIN32
    out.Number("cpu_ms", int64_t((CpuTicks() - cpu_start) / 10000));
    IO_COUNTERS io{};
    if (GetProcessIoCounters(GetCurrentProcess(), &io)) {
      out.Number("read_bytes", int64_t(io.ReadTransferCount - io_start.ReadTransferCount));
      out.Number("read_operations", int64_t(io.ReadOperationCount - io_start.ReadOperationCount));
      out.Number("other_operations", int64_t(io.OtherOperationCount - io_start.OtherOperationCount));
    }
    PROCESS_MEMORY_COUNTERS memory{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
      out.Number("peak_working_set", int64_t(memory.PeakWorkingSetSize));
#endif
    out.EndObject();
    auto json = out.Take();
    const auto detail = stages.Take();
    json.insert(json.find_last_of('}'), "," + detail.substr(1, detail.find_last_of('}') - 1));
    std::cout << json << std::endl;
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << std::endl; return 1; }
}
