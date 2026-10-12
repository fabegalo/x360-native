// ReXApp teardown after a successful setup, and after a failed one.
//
// dash_17559 with patches 0001-0003 (docs/dashboard.md §0.7 B): ReXApp::OnDestroy
// destroyed the window (window_.reset()) before the runtime. The mouse/keyboard driver,
// attached to the window after a successful setup (rex_app.cpp, AttachWindow), used
// it from ~MnkInputDriver (DetachFromWindow) during Runtime::Shutdown: access
// violation under a debugger, STATUS_HEAP_CORRUPTION without one. The SDL window is
// destroyed without OnClosing to its listeners, so the driver never learnt of it.
// patches/rexglue-sdk/0004 shuts the runtime down while the window still exists,
// after the drawers made from its graphics device are released and the presenter is
// detached from the window, and destroys the window last.
//
// The probe (tests/shutdown_probe) runs a synthetic XEX whose entry point returns at
// once; the app quits by itself, as the dashboard did. The failed-setup case keeps the
// path of patch 0002 covered through the real ReXApp.
#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "synthetic_xex.h"
#include "session_window.h"
#include "child_process.h"

using namespace x360n::test;

namespace {

namespace fs = std::filesystem;

struct ProbeResult {
  bool finished = false;  // ended by itself before the timeout
  DWORD exit_code = 0;
  std::string log;
};

ProbeResult RunProbe(const Workdir& w, const std::wstring& extra_args) {
  const fs::path probe = fs::path(X360N_SHUTDOWN_PROBE).make_preferred();
  REQUIRE(fs::is_regular_file(probe));
  const fs::path log = w.dir / "probe.log";
  // Reuse the supervisor's debugger for native callback/teardown failures.
  x360n::ProcessSpec spec;
  spec.exe = probe; spec.working_dir = w.dir;
  spec.stdout_file = w.dir / "stdout.log"; spec.stderr_file = w.dir / "stderr.log";
  spec.timeout = std::chrono::seconds(60); spec.capture_crash = extra_args.find(L"--parent_window=") != std::wstring::npos;
  spec.args = {"--game_data_root=" + (w.dir / "game").string(),
               "--user_data_root=" + (w.dir / "user").string(),
               "--log_file=" + log.string(), "--log_level=debug"};
  std::wistringstream extras(extra_args);
  for (std::wstring arg; extras >> arg;) spec.args.emplace_back(arg.begin(), arg.end());
  const auto child = x360n::RunProcess(spec);
  REQUIRE(child.launched);
  ProbeResult r;
  r.finished = !child.timed_out;
  r.exit_code = child.exit_code;
  std::ifstream in(log);
  std::stringstream ss;
  ss << in.rdbuf();
  r.log = ss.str();
  for (const auto& frame : child.crash.frames) r.log += "\n" + frame;
  return r;
}

bool Contains(const std::string& s, const char* what) {
  return s.find(what) != std::string::npos;
}

void PrepareGameRoot(const Workdir& w) {
  fs::create_directories(w.dir / "game");
  fs::create_directories(w.dir / "user");
  std::vector<Placed> placed;
  const auto xex = BuildXex({}, &placed, 0, Entry::kReturnZero);
  std::ofstream(w.dir / "game" / "default.xex", std::ios::binary)
      .write(reinterpret_cast<const char*>(xex.data()), std::streamsize(xex.size()));
}

}  // namespace

TEST_CASE("ReXApp shuts down cleanly when the guest returns after a successful setup",
          "[sdk][app]") {
  Workdir w("x360n_app_shutdown_test", "exit");
  PrepareGameRoot(w);

  SECTION("without a GPU plugin (window and input)") {
    const auto r = RunProbe(w, L"");
    INFO("exit code 0x" << std::hex << r.exit_code);
    REQUIRE(r.finished);
    CHECK(Contains(r.log, "Execution complete"));  // the guest entry point ran and returned
    CHECK_FALSE(Contains(r.log, "Runtime setup failed"));
    CHECK(r.exit_code == 0);
  }

  SECTION("with the xenos GPU plugin (D3D12 device, presenter, drawers)") {
    const auto r = RunProbe(w, L"--gpu_plugin=xenos");
    INFO("exit code 0x" << std::hex << r.exit_code);
    REQUIRE(r.finished);
    CHECK(Contains(r.log, "Execution complete"));
    CHECK_FALSE(Contains(r.log, "Runtime setup failed"));
    CHECK(r.exit_code == 0);
  }
}

TEST_CASE("ReXApp shuts down cleanly after a failed Runtime::Setup", "[sdk][app]") {
  Workdir w("x360n_app_shutdown_test", "setup_failure");
  PrepareGameRoot(w);
  const auto r = RunProbe(w, L"--gpu_plugin=xenos --x360n_probe_fail_setup=true");
  INFO("exit code 0x" << std::hex << r.exit_code);
  REQUIRE(r.finished);
  CHECK(Contains(r.log, "Runtime setup failed"));
  CHECK_FALSE(Contains(r.log, "Execution complete"));
  CHECK(r.exit_code == EXIT_FAILURE);  // OnInitialize failed; no unhandled exception
}

TEST_CASE("title surfaces reuse a persistent host window across guest exits", "[sdk][app][parent-window]") {
  x360n::SessionWindow host(false);
  const auto handle = host.handle();
  for (unsigned i = 0; i < 2; ++i) {
    Workdir w("x360n_parent_window", std::to_string(i).c_str());
    PrepareGameRoot(w);
    const auto r = RunProbe(w, L"--gpu_plugin=xenos --parent_window=" + std::to_wstring(handle));
    INFO(r.log);
    REQUIRE(r.finished);
    CHECK(r.exit_code == 0);
    CHECK(Contains(r.log, "parent_window attached:"));
    CHECK(Contains(r.log, "Execution complete"));
    CHECK(IsWindow(reinterpret_cast<HWND>(handle)));
    CHECK_FALSE(host.closed());
    CHECK(host.attached_count() == i + 1);
  }
}

TEST_CASE("embedded SDL surface recognizes actual keyboard focus and releases it", "[sdk][app][child-focus]") {
  x360n::SessionWindow host;
  Workdir w("x360n_child_focus", "native");
  PrepareGameRoot(w);
  const auto r = RunProbe(w, L"--x360n_probe_child_focus=true --parent_window=" +
                            std::to_wstring(host.handle()));
  INFO(r.log);
  REQUIRE(r.finished);
  CHECK(r.exit_code == 0);
  if (Contains(r.log, "child-focus foreground root=false")) {
    SKIP("Windows denied foreground activation; focus integration requires an interactive foreground session");
  }
  CHECK(Contains(r.log, "child-focus gain: native=true, sdk=true"));
  CHECK(Contains(r.log, "child-focus loss: native=true, sdk=true"));
  CHECK(Contains(r.log, "child-focus regain: native=true, sdk=true"));
}
