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
  std::wstring cmd = L"\"" + probe.wstring() + L"\" --game_data_root=\"" +
                     (w.dir / "game").wstring() + L"\" --user_data_root=\"" +
                     (w.dir / "user").wstring() + L"\" --log_file=\"" + log.wstring() +
                     L"\" --log_level=debug " + extra_args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  // A crashing child must not wait on a Windows Error Reporting dialog; the child
  // inherits this error mode.
  const UINT old_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const BOOL created = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                      w.dir.c_str(), &si, &pi);
  SetErrorMode(old_mode);
  REQUIRE(created);
  ProbeResult r;
  r.finished = WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0;
  if (!r.finished) {
    TerminateProcess(pi.hProcess, 0xDEAD);
    WaitForSingleObject(pi.hProcess, 10000);
  }
  GetExitCodeProcess(pi.hProcess, &r.exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  std::ifstream in(log);
  std::stringstream ss;
  ss << in.rdbuf();
  r.log = ss.str();
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
