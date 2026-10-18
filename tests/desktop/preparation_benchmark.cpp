// Bounded official preparation, with complete process-tree Job Object metrics.
// Compiles titles when required; never executes them.
#include "child_process.h"
#include "report.h"
#include "paths.h"
#include <iostream>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace x360n;
int main(int argc, char** argv) {
  if (argc != 4 || !IsSafeLabel(argv[2])) return 2;
  try {
  const auto root = std::filesystem::absolute(PathFromUtf8(argv[1]));
  const auto output = std::filesystem::absolute(PathFromUtf8(argv[3]));
  if (std::filesystem::exists(output)) return 2;
  std::filesystem::create_directories(output);
  ProcessSpec spec;
#ifdef _WIN32
  wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
  spec.exe = std::filesystem::path(system) / "WindowsPowerShell/v1.0/powershell.exe";
#else
  return 3;
#endif
  spec.args = {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", PathUtf8(root / "scripts/build-title.ps1"), "-Title", argv[2]};
  spec.working_dir = root;
  spec.stdout_file = output / "stdout.log"; spec.stderr_file = output / "stderr.log";
  spec.timeout = std::chrono::minutes(20); spec.no_console = true; spec.collect_metrics = true;
  const auto result = RunProcess(spec);
  JsonWriter out;
  out.BeginObject().String("title", argv[2]).Number("duration_ms", result.duration.count())
     .Number("exit_code", int64_t(result.exit_code)).Bool("launched", result.launched).Bool("timed_out", result.timed_out);
  if (!result.launch_error.empty()) out.String("error", result.launch_error);
  if (result.metrics) {
    const auto& m = *result.metrics;
    out.Number("cpu_us", int64_t(m.cpu_us)).Number("read_bytes", int64_t(m.read_bytes))
       .Number("read_operations", int64_t(m.read_operations)).Number("write_bytes", int64_t(m.write_bytes))
       .Number("peak_job_commit_bytes", int64_t(m.peak_job_commit_bytes));
  } else {
    out.Null("cpu_us").Null("read_bytes").Null("read_operations").Null("write_bytes").Null("peak_job_commit_bytes");
    out.String("metrics_unavailable_reason", "Windows Job Object accounting unavailable");
  }
  out.EndObject(); const auto json = out.Take();
  std::ofstream(output / "result.json", std::ios::binary) << json;
  std::cout << json << std::endl;
  return result.launched && !result.timed_out ? int(result.exit_code) : 3;
  } catch (const std::exception& error) { std::cerr << error.what() << std::endl; return 3; }
}
