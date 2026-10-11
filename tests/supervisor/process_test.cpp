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

TEST_CASE("kills the child on timeout", "[process]") {
  Scratch s;
  auto r = RunProcess(s.Spec({"sleep", "20000"}, 500ms));
  REQUIRE(r.launched);
  CHECK(r.timed_out);
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
}
