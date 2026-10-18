// Supervisor process tests. They launch x360n_test_child (a scripted fixture),
// so they validate the supervisor only, not any title or the dashboard.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "child_process.h"
#include "scratch.h"

namespace fs = std::filesystem;
using namespace x360n;
using namespace std::chrono_literals;

namespace {

std::string Slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Scratch {
  fs::path dir = UniqueTempPath("process_test");
  Scratch() {
    fs::remove_all(dir);
    fs::create_directories(dir);
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  ProcessSpec Spec(std::vector<std::string> args, std::chrono::milliseconds timeout = 30s) const {
    return {X360N_TEST_CHILD, std::move(args), dir, dir / "out.log", dir / "err.log", timeout};
  }
};

}  // namespace

TEST_CASE("captures stdout, stderr and the exit code", "[process]") {
  Scratch s;
  auto r = RunProcess(s.Spec({"exit", "7"}));
  REQUIRE(r.launched);
  CHECK_FALSE(r.timed_out);
  CHECK(r.exit_code == 7);
  CHECK(Slurp(s.dir / "out.log").find("child stdout") != std::string::npos);
  CHECK(Slurp(s.dir / "err.log").find("child stderr") != std::string::npos);
}

#ifdef _WIN32
TEST_CASE("desktop child has no console and retains captured output", "[process]") {
  Scratch s;
  auto spec = s.Spec({"console"});
  spec.no_console = true;
  auto result = RunProcess(spec);
  REQUIRE(result.launched);
  CHECK(result.exit_code == 0);
  CHECK(Slurp(s.dir / "out.log").find("console=0") != std::string::npos);
}
#endif

TEST_CASE("kills the child on timeout", "[process]") {
  Scratch s;
  auto r = RunProcess(s.Spec({"sleep", "20000"}, 500ms));
  REQUIRE(r.launched);
  CHECK(r.timed_out);
  CHECK(r.duration < 10s);
}

TEST_CASE("session cancellation stops the child without reporting a timeout", "[process]") {
  Scratch s;
  auto spec = s.Spec({"sleep", "20000"});
  bool started = false;
  spec.on_started = [&] { started = true; };
  spec.should_cancel = [&] { return started; };
  const auto r = RunProcess(spec);
  REQUIRE(r.launched);
  CHECK(r.cancelled);
  CHECK_FALSE(r.timed_out);
  CHECK(r.duration < 10s);
}

TEST_CASE("reports a crash as an NTSTATUS code without a dialog", "[process]") {
  Scratch s;
  auto r = RunProcess(s.Spec({"crash"}));
  REQUIRE(r.launched);
  CHECK(r.exit_code == 0xC0000005u);
}

TEST_CASE("launch failure is reported, not hidden", "[process]") {
  Scratch s;
  ProcessSpec spec = s.Spec({});
  spec.exe = s.dir / "no-such-program.exe";
  auto r = RunProcess(spec);
  CHECK_FALSE(r.launched);
  CHECK_FALSE(r.launch_error.empty());
}

// End to end through the x360n CLI: exit-code mapping, refusal of non-build
// executables, and the JSON report including stub counts.
TEST_CASE("x360n run maps outcomes to its exit codes and writes a report", "[cli]") {
  Scratch s;
  auto run = [&](std::vector<std::string> args) {
    ProcessSpec spec{X360N_EXE, std::move(args), X360N_SOURCE_ROOT, s.dir / "cli.out", s.dir / "cli.err", 60s};
    auto r = RunProcess(spec);
    REQUIRE(r.launched);
    return r.exit_code;
  };
  CHECK(run({"run", X360N_TEST_CHILD, "--label", "selftest-ok", "--", "exit", "0"}) == 0);
  CHECK(run({"run", X360N_TEST_CHILD, "--label", "selftest-fail", "--", "exit", "3"}) == 1);
  CHECK(run({"run", X360N_TEST_CHILD, "--label", "selftest-timeout", "--timeout", "1", "--", "sleep", "20000"}) == 2);
  // Not a build output: refused before launch.
  std::ofstream(s.dir / "outside.exe") << "x";
  CHECK(run({"run", (s.dir / "outside.exe").string()}) == 3);

  CHECK(run({"run", X360N_TEST_CHILD, "--label", "selftest-stubs", "--", "stub-log"}) == 0);
  std::string out = Slurp(s.dir / "cli.out");
  auto pos = out.find("report ");
  REQUIRE(pos != std::string::npos);
  std::string report_path = out.substr(pos + 7);
  while (!report_path.empty() && (report_path.back() == '\n' || report_path.back() == '\r'))
    report_path.pop_back();
  std::string report = Slurp(report_path);
  CHECK(report.find("\"stub_lines\": 3") != std::string::npos);
  CHECK(report.find("\"__imp__XamFoo\": 2") != std::string::npos);
  CHECK(report.find("\"exit_code\": 0") != std::string::npos);
  CHECK(report.find("\"sdk_commit_locked\": \"" X360N_SDK_COMMIT_LOCKED "\"") != std::string::npos);
  CHECK(report.find("\"dlls\": [") != std::string::npos);

  // --rex-log: the child logs to <run dir>/rex.log (stdout stays empty) and the
  // stub scan must still see it.
  CHECK(run({"run", X360N_TEST_CHILD, "--label", "selftest-rexlog", "--rex-log", "--", "stub-log"}) == 0);
  out = Slurp(s.dir / "cli.out");
  pos = out.find("report ");
  REQUIRE(pos != std::string::npos);
  report_path = out.substr(pos + 7);
  while (!report_path.empty() && (report_path.back() == '\n' || report_path.back() == '\r'))
    report_path.pop_back();
  report = Slurp(report_path);
  CHECK(report.find("\"rex_log\": ") != std::string::npos);
  CHECK(report.find("\"stub_lines\": 3") != std::string::npos);
  CHECK(Slurp(fs::path(report_path).parent_path() / "stdout.log").empty());

  // A timeout is reported as the supervisor's doing, not as the child's code.
  CHECK(run({"run", X360N_TEST_CHILD, "--label", "selftest-timeout2", "--timeout", "1", "--", "sleep", "20000"}) == 2);
  CHECK(Slurp(s.dir / "cli.out").find("TIMEOUT, terminated by the supervisor") != std::string::npos);

  // The child runs in its executable's directory (not the caller's, here the
  // source root), and the report and the console line say so.
  CHECK(run({"run", X360N_TEST_CHILD, "--label", "selftest-cwd", "--", "cwd"}) == 0);
  out = Slurp(s.dir / "cli.out");
  CHECK(out.find("(working dir ") != std::string::npos);
  pos = out.find("report ");
  REQUIRE(pos != std::string::npos);
  report_path = out.substr(pos + 7);
  while (!report_path.empty() && (report_path.back() == '\n' || report_path.back() == '\r'))
    report_path.pop_back();
  std::string child_out = Slurp(fs::path(report_path).parent_path() / "stdout.log");
  REQUIRE(child_out.rfind("cwd ", 0) == 0);
  std::string child_cwd = child_out.substr(4);
  while (!child_cwd.empty() && (child_cwd.back() == '\n' || child_cwd.back() == '\r'))
    child_cwd.pop_back();
  CHECK(fs::equivalent(child_cwd, fs::path(X360N_TEST_CHILD).parent_path()));
  CHECK(Slurp(report_path).find("\"working_dir\": ") != std::string::npos);
}

// --crash-stack: the child runs under the debug API; an unhandled access
// violation is recorded with its stack (symbols from the fixture's PDB), and the
// child still dies with the exception as its exit code. A clean exit records none.
TEST_CASE("x360n run --crash-stack records the stack of an unhandled exception", "[cli]") {
  Scratch s;
  auto run = [&](std::vector<std::string> args) {
    ProcessSpec spec{X360N_EXE, std::move(args), X360N_SOURCE_ROOT, s.dir / "cli.out", s.dir / "cli.err", 60s};
    auto r = RunProcess(spec);
    REQUIRE(r.launched);
    return r.exit_code;
  };
  auto report_of = [&]() {
    std::string out = Slurp(s.dir / "cli.out");
    auto pos = out.rfind("report ");
    REQUIRE(pos != std::string::npos);
    std::string path = out.substr(pos + 7);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    return fs::path(path);
  };

  CHECK(run({"run", X360N_TEST_CHILD, "--label", "selftest-crashstack", "--crash-stack", "--", "crash"}) == 1);
  fs::path report = report_of();
  std::string json = Slurp(report);
  CHECK(json.find("\"exit_code_hex\": \"0xC0000005\"") != std::string::npos);
  CHECK(json.find("\"exception_code\": \"0xC0000005\"") != std::string::npos);
  CHECK(json.find("\"access\": \"write 0x0000000000000000\"") != std::string::npos);
  std::string crash = Slurp(report.parent_path() / "crash.txt");
  INFO(crash);
  CHECK(crash.find("x360n_test_child!main+") != std::string::npos);
  CHECK(crash.find("test_child.cpp:") != std::string::npos);

  CHECK(run({"run", X360N_TEST_CHILD, "--label", "selftest-crashstack-ok", "--crash-stack", "--", "exit", "0"}) == 0);
  json = Slurp(report_of());
  CHECK(json.find("\"captured\": false") != std::string::npos);
  CHECK_FALSE(fs::exists(report_of().parent_path() / "crash.txt"));
}
