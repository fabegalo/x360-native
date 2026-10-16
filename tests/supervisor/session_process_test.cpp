#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include "child_process.h"
#include "session_profile.h"
#include "console_settings.h"
#include "preparation_fixture.h"
#include "scratch.h"
#include <algorithm>
#include <sstream>

using namespace x360n;
namespace fs = std::filesystem;
using namespace std::chrono_literals;
namespace {
std::string Read(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
struct Fixture {
  fs::path root = PathFromUtf8(X360N_SOURCE_ROOT) / ".local/tmp" /
      UniqueTempPath("session-process").filename() / fs::path(u8"João 日本 🎮");
  ProjectPaths paths{root};
  Fixture(const std::string& mode) {
    fs::create_directories(paths.build());
    fs::create_directories(paths.inputs());
    for (const std::string role : {"dashboard", "game"}) {
      auto exe = paths.build() / (role + ".exe");
      fs::copy_file(PathFromUtf8(X360N_SESSION_CHILD), exe, fs::copy_options::overwrite_existing);
      auto xex = paths.inputs() / (role + ".xex");
      std::ofstream(xex) << role;
      auto manifest = root / "titles" / role / (role + "_manifest.toml");
      fs::create_directories(manifest.parent_path());
      std::ofstream out(manifest);
      out << "[project]\nname = '" << role << "'\ngame_root = '../../.local/inputs'\n"
          << "[entrypoint]\nfile_path = '../../.local/inputs/" << role << ".xex'\n"
          << "[x360n]\nnative_exe = '../../.local/build/" << role << ".exe'\n"
          << "user_data_root = '../../.local/user/" << role << "'\nxex_sha256 = '" << DigestFile(xex)->sha256 << "'\n"
          << "args = ['--gpu_plugin=native', '--role=" << role << "', '--mode=" << mode << "']\n";
      if (role == "dashboard") out << "launch_targets = ['../game/game_manifest.toml']\ndashboard_return_flags = [256]\n";
      out.close();
      WriteTestReceipt(paths, role, manifest, exe, {xex});
    }
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root.parent_path(), ec); }
  ProcessResult Run(int timeout = 0) {
    return RunProcess({PathFromUtf8(X360N_EXE), {"--root", PathUtf8(root), "session",
        PathUtf8(root / "titles/dashboard/dashboard_manifest.toml"), "--separate-windows", "--timeout", std::to_string(timeout)},
        root, root / "out.log", root / "err.log", 90s});
  }
};
}
TEST_CASE("console preferences survive title handoffs and reload on session restart", "[console-settings][cli]") {
  Fixture f("locale");
  SaveConsoleSettings(f.paths, {9, 13});
  auto result = f.Run();
  INFO(Read(f.root / "err.log"));
  REQUIRE(result.exit_code == 0);
  CHECK(Read(f.paths.inputs() / "console-observed.txt") == "9 13\n9 13\n9 13\n");
  // First child changes the saved preferences. The running session retains its
  // snapshot, while a new supervisor process reads the new values.
  result = f.Run();
  REQUIRE(result.exit_code == 0);
  CHECK(Read(f.paths.inputs() / "console-observed.txt") == "9 13\n9 13\n9 13\n1 103\n1 103\n1 103\n");
}
TEST_CASE("session dashboard version reaches the game and dashboard return", "[games-played][cli]") {
  for (const auto* version : {"2.0.6717.0", "2.0.17559.0"}) {
    Fixture f("version");
    const auto manifest = f.root / "titles/dashboard/dashboard_manifest.toml";
    std::ofstream(manifest.parent_path() / "preparation.toml")
        << "kind='dashboard'\ndashboard_version='" << version << "'\n";
    WriteTestReceipt(f.paths, "dashboard", manifest, f.paths.build() / "dashboard.exe",
                     {f.paths.inputs() / "dashboard.xex"});
    auto result = f.Run();
    INFO(Read(f.root / "err.log"));
    REQUIRE(result.exit_code == 0);
    const auto line = std::string(version) + "\n";
    CHECK(Read(f.paths.inputs() / "version-observed.txt") == line + line + line);
  }
}
TEST_CASE("session survives 80 transitions with intact handoffs and states", "[session][cli]") {
  Fixture f("healthy");
  auto result = f.Run();
  INFO(Read(f.root / "err.log"));
  INFO(Read(f.root / "out.log"));
  INFO(Read(f.paths.inputs() / "observed.txt"));
  REQUIRE(result.launched);
  REQUIRE_FALSE(result.timed_out);
  REQUIRE(result.exit_code == 0);
  std::istringstream states(Read(f.paths.inputs() / "observed.txt"));
  for (unsigned step = 0; step <= 80; ++step) {
    unsigned observed; bool dashboard;
    REQUIRE(bool(states >> observed >> dashboard));
    CHECK(observed == step);
    CHECK(dashboard == (step % 2 == 0));
    auto suffix = "-session-" + std::to_string(step) + (dashboard ? "-dashboard" : "-game");
    unsigned found = 0;
    for (const auto& run : fs::directory_iterator(f.paths.runs())) {
      if (!PathUtf8(run.path().filename()).ends_with(suffix)) continue;
      ++found;
      LaunchRequest request;
      REQUIRE(ReadLaunchRequest(run.path() / "handoff.bin", request));
      CHECK(request.kind == (step == 80 ? LaunchRequest::WindowClose :
                            dashboard ? LaunchRequest::Image : LaunchRequest::Dashboard));
      CHECK(request.flags == (dashboard ? 0 : 0x100));
      CHECK(request.data_present);
      CHECK(request.data == std::vector<uint8_t>{uint8_t(step), 0, 0xFE, 0xFF});
      CHECK(request.path == (dashboard && step != 80 ? PathUtf8(fs::canonical(f.paths.inputs() / "game.xex")) : ""));
      CHECK(Read(run.path() / "report.json").find("\"exit_code\": 0") != std::string::npos);
    }
    CHECK(found == 1);
  }
  std::string extra; CHECK_FALSE(bool(states >> extra));
}
TEST_CASE("session stops rapid loops and never retries failures or invalid transitions", "[session][cli]") {
  for (const std::string mode : {"rapid", "failure", "timeout", "missing", "invalid-flags", "invalid-target", "tamper"}) {
    CAPTURE(mode);
    Fixture f(mode);
    auto result = f.Run(mode == "timeout" ? 1 : 0);
    INFO(Read(f.root / "err.log"));
    REQUIRE(result.launched);
    CHECK_FALSE(result.timed_out);
    CHECK(result.exit_code == (mode == "timeout" ? 2 : 1));
    auto states = Read(f.paths.inputs() / "observed.txt");
    CHECK(std::count(states.begin(), states.end(), '\n') == (mode == "rapid" ? 32 : mode == "tamper" ? 3 : 2));
    if (mode == "rapid") CHECK(Read(f.root / "err.log").find("without sustained execution") != std::string::npos);
  }
}
