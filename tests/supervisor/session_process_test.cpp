#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include "child_process.h"
#include "session_profile.h"
#include "console_settings.h"
#include "renderer_settings.h"
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
  ProcessResult Run(int timeout = 0, bool crash_stack = false, const fs::path& user_root = {}) {
    std::vector<std::string> args{"--root", PathUtf8(root), "session",
        PathUtf8(root / "titles/dashboard/dashboard_manifest.toml"), "--separate-windows", "--timeout", std::to_string(timeout)};
    if (crash_stack) args.push_back("--crash-stack");
    if (!user_root.empty()) { args.push_back("--user-data-root"); args.push_back(PathUtf8(user_root)); }
    return RunProcess({PathFromUtf8(X360N_EXE), std::move(args),
        root, root / "out.log", root / "err.log", 90s});
  }
};
}
TEST_CASE("firmware powerdown closes the platform from either session participant", "[firmware-exit][cli]") {
  for (const std::string mode : {"firmware-dashboard", "firmware-child"}) {
    Fixture f(mode);
    const auto result = f.Run(10);
    INFO(Read(f.root / "err.log"));
    REQUIRE(result.exit_code == 0);
    std::istringstream observed(Read(f.paths.inputs() / "observed.txt"));
    unsigned step = 99; bool dashboard = false;
    REQUIRE(bool(observed >> step >> dashboard));
    CHECK(step == 0); CHECK(dashboard);
    if (mode == "firmware-child") {
      REQUIRE(bool(observed >> step >> dashboard));
      CHECK(step == 1); CHECK_FALSE(dashboard);
    }
    CHECK_FALSE(bool(observed >> step));
    CHECK(Read(f.root / "out.log").find("requested firmware exit (routine 5)") != std::string::npos);
  }
}
TEST_CASE("dashboard return from the dashboard preserves context instead of closing the platform",
          "[dashboard-self-return][cli]") {
  for (const std::string mode : {"dashboard-self-return", "dashboard-self-invalid"}) {
    Fixture f(mode);
    const auto result = f.Run(10);
    INFO(Read(f.root / "err.log")); INFO(Read(f.root / "out.log"));
    REQUIRE(result.launched); REQUIRE_FALSE(result.timed_out);
    std::istringstream observed(Read(f.paths.inputs() / "observed.txt"));
    unsigned step = 99; bool dashboard = false;
    REQUIRE(bool(observed >> step >> dashboard));
    CHECK(step == 0); CHECK(dashboard);
    if (mode == "dashboard-self-return") {
      REQUIRE(result.exit_code == 0);
      // The second dashboard validates the exact previous launch data and
      // profile root before recording its entry, then requests WindowClose.
      REQUIRE(bool(observed >> step >> dashboard));
      CHECK(step == 1); CHECK(dashboard);
      CHECK(Read(f.root / "out.log").find("requested dashboard; returning to dashboard") != std::string::npos);
    } else {
      CHECK(result.exit_code != 0);
      CHECK(Read(f.root / "err.log").find("dashboard return flags 00000200 are not implemented") != std::string::npos);
    }
    CHECK_FALSE(bool(observed >> step));
  }
}
TEST_CASE("session storage override follows handoffs without changing global storage", "[session-storage][cli]") {
  Fixture f("storage");
  const auto copy = f.paths.local() / "isolated";
  fs::create_directories(copy);
  const auto result = f.Run(10, false, copy);
  INFO(Read(f.root / "err.log"));
  INFO(Read(f.root / "out.log"));
  REQUIRE(result.exit_code == 0);
  CHECK(Read(f.paths.inputs() / "storage-observed.txt") ==
        PathUtf8(copy / "dashboard") + "\n" + PathUtf8(copy / "game") + "\n" + PathUtf8(copy / "dashboard") + "\n");
  CHECK_FALSE(fs::exists(f.paths.local() / "user-data-root.txt"));
  for (const auto& invalid : {f.paths.inputs(), f.paths.build(), f.root / "titles", copy / "absent", fs::path("relative")}) {
    CAPTURE(invalid);
    CHECK(f.Run(10, false, invalid).exit_code != 0);
  }
  CHECK(Read(f.paths.inputs() / "storage-observed.txt").find("relative") == std::string::npos);
}
TEST_CASE("session captures an unhandled exception only with explicit crash diagnostics", "[session-crash][cli]") {
  for (const bool capture : {false, true}) {
    for (const std::string mode : {"crash-dashboard", "crash-game"}) {
      CAPTURE(capture, mode);
      Fixture f(mode);
      auto result = f.Run(10, capture);
      INFO(Read(f.root / "err.log"));
      REQUIRE(result.launched);
      REQUIRE_FALSE(result.timed_out);
      REQUIRE(result.exit_code != 0);
      unsigned runs = 0, crashes = 0;
      for (const auto& run : fs::directory_iterator(f.paths.runs())) {
        ++runs;
        const auto report = Read(run.path() / "report.json");
        if (report.find("\"exit_code_hex\": \"0xC0000005\"") == std::string::npos) continue;
        ++crashes;
        CHECK(fs::exists(run.path() / "crash.txt") == capture);
        CHECK((report.find("\"exception_code\": \"0xC0000005\"") != std::string::npos) == capture);
      }
      CHECK(crashes == 1);
      // No relaunch after a crash, including a crash before the first handoff.
      CHECK(runs == (mode == "crash-dashboard" ? 1 : 2));
    }
  }
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
  INFO(Read(f.root / "err.log"));
  INFO(Read(f.root / "out.log"));
  REQUIRE(result.exit_code == 0);
  CHECK(Read(f.paths.inputs() / "console-observed.txt") == "9 13\n9 13\n9 13\n1 103\n1 103\n1 103\n");
}
TEST_CASE("session dashboard version reaches the game and dashboard return", "[games-played][cli]") {
  for (const auto* version : {"2.0.6717.0", "2.0.17559.0"}) {
    Fixture f("version");
    const auto manifest = f.root / "titles/dashboard/dashboard_manifest.toml";
    std::ofstream(manifest.parent_path() / "preparation.toml")
        << "label='Dashboard fixture'\nlimitations='Synthetic session test'\n"
        << "kind='dashboard'\ndashboard_version='" << version << "'\n"
        << "[[files]]\npath='dashboard.xex'\nsource_name='dashboard.xex'\nsha256='"
        << DigestFile(f.paths.inputs() / "dashboard.xex")->sha256 << "'\n";
    WriteTestReceipt(f.paths, "dashboard", manifest, f.paths.build() / "dashboard.exe",
                     {f.paths.inputs() / "dashboard.xex"});
    auto result = f.Run();
    INFO(Read(f.root / "err.log"));
    REQUIRE(result.exit_code == 0);
    const auto line = std::string(version) + "\n";
    CHECK(Read(f.paths.inputs() / "version-observed.txt") == line + line + line);
  }
}
TEST_CASE("session hands only its dashboard's verified avatar files to an avatar title", "[avatar][cli]") {
  for (const std::string variant : {"valid", "missing", "different", "version"}) {
    CAPTURE(variant);
    Fixture f("avatar");
    const auto dashboard = f.root / "titles/dashboard/dashboard_manifest.toml";
    const auto game = f.root / "titles/game/game_manifest.toml";
    const auto xam = f.paths.inputs() / "system/xam_ui.xex";
    const auto catalog = f.paths.inputs() / "avatar/AvatarAssetPack.toc";
    fs::create_directories(xam.parent_path());
    fs::create_directories(catalog.parent_path());
    std::ofstream(xam, std::ios::binary) << "xam 17559 fixture";
    std::ofstream(catalog, std::ios::binary) << "catalog 17559 fixture";
    const auto xam_sha = DigestFile(xam)->sha256;
    const std::string version = variant == "version" ? "2.0.6717.0" : "2.0.17559.0";
    // The dashboard's system UI host binds the XAM image the service shares.
    std::ofstream(f.paths.build() / "session_ui.dll", std::ios::binary) << "host fixture";
    std::ofstream(dashboard, std::ios::app) << "system_ui_host = 'session_ui.dll'\n";
    std::ofstream(dashboard.parent_path() / "preparation.toml")
        << "label='Dashboard fixture'\nlimitations='Synthetic avatar test'\n"
        << "kind='dashboard'\ndashboard_version='" << version << "'\nruntime_files=['session_ui.dll']\n"
        << "[[files]]\npath='system/xam_ui.xex'\nsource_name='xam.xex'\nsha256='" << xam_sha << "'\n"
        << "[[files]]\npath='avatar/AvatarAssetPack.toc'\nsource_name='AvatarAssetPack.toc'\nsha256='"
        << DigestFile(catalog)->sha256 << "'\noptional=true\n"
        << "[avatar]\nversion='" << version << "'\nxam.path='system/xam_ui.xex'\nxam.provenance='fixture'\n"
        << "catalog.path='avatar/AvatarAssetPack.toc'\ncatalog.provenance='fixture'\n";
    WriteTestReceipt(f.paths, "dashboard", dashboard, f.paths.build() / "dashboard.exe",
                     {f.paths.inputs() / "dashboard.xex"});
    std::ofstream(game, std::ios::app) << "avatar_service = '2.0.17559.0'\navatar_xam_sha256 = '" << xam_sha << "'\n";
    WriteTestReceipt(f.paths, "game", game, f.paths.build() / "game.exe", {f.paths.inputs() / "game.xex"});
    if (variant == "missing") fs::remove(catalog);
    if (variant == "different") std::ofstream(catalog, std::ios::binary) << "another catalog";
    auto result = f.Run();
    const auto err = Read(f.root / "err.log");
    INFO(err);
    REQUIRE(result.launched);
    const auto observed = Read(f.paths.inputs() / "avatar-observed.txt");
    if (variant == "valid") {
      REQUIRE(result.exit_code == 0);
      // The dashboard itself receives nothing; the title receives both paths.
      CHECK(observed == "1 |\n0 " + PathUtf8(fs::canonical(xam)) + "|" + PathUtf8(fs::canonical(catalog)) +
                        "\n1 |\n");
      continue;
    }
    // Refused before the title starts: only the dashboard ran.
    CHECK(result.exit_code == 3);
    CHECK(observed == "1 |\n");
    CHECK(err.find("avatar service 2.0.17559.0 of game") != std::string::npos);
    if (variant == "version") {
      CHECK(err.find("is 2.0.6717.0") != std::string::npos);
    } else {
      CHECK(err.find(variant == "missing" ? "missing AvatarAssetPack.toc 2.0.17559.0"
                                          : "different file instead of AvatarAssetPack.toc 2.0.17559.0") !=
            std::string::npos);
    }
  }
}
TEST_CASE("graphics API is immutable through dashboard game dashboard transitions", "[session-graphics][cli]") {
  Fixture f("graphics");
  const auto result = f.Run();
  INFO(Read(f.root / "err.log"));
  REQUIRE(result.launched);
  REQUIRE_FALSE(result.timed_out);
  REQUIRE(result.exit_code == 0);
  CHECK(Read(f.paths.inputs() / "graphics-observed.txt") == "d3d12\nd3d12\nd3d12\n");
  // The child's new preference applies only on the next platform session.
  CHECK(LoadRendererSettings(f.paths).api == GraphicsApi::Vulkan);
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

TEST_CASE("stable session crash and relaunch preserve the single HDD bytes", "[stability][cli]") {
  Fixture f("crash-game");
  // Opaque synthetic payloads: this checks supervisor protection, not a game's
  // save format or XAM persistence (covered separately by C8).
  const auto hdd = f.paths.local() / "user/dashboard/Content";
  const std::vector<fs::path> files{
      hdd / "E000000000000001/FFFE07D1/00010000/profile",
      hdd / "E000000000000001/1234C800/00000001/progress/data",
      hdd / "E000000000000002/1234C800/00000001/progress/data",
      hdd / "E000000000000001/1234C801/00000001/progress/data"};
  std::vector<std::string> hashes;
  for (size_t i = 0; i < files.size(); ++i) {
    fs::create_directories(files[i].parent_path());
    std::ofstream(files[i], std::ios::binary) << "committed fixture " << i;
    hashes.push_back(DigestFile(files[i])->sha256);
  }
  auto result = f.Run(10);
  REQUIRE(result.launched);
  REQUIRE_FALSE(result.timed_out);
  REQUIRE(result.exit_code != 0);
  for (size_t i = 0; i < files.size(); ++i)
    REQUIRE(DigestFile(files[i])->sha256 == hashes[i]);
  // Explicit user relaunch after the crash; no automatic crash retry. Replace
  // only the synthetic participant mode and publish fresh fixture receipts.
  for (const std::string role : {"dashboard", "game"}) {
    const auto manifest = f.root / "titles" / role / (role + "_manifest.toml");
    auto text = Read(manifest);
    const auto pos = text.find("crash-game");
    REQUIRE(pos != std::string::npos);
    text.replace(pos, 10, "version");
    std::ofstream(manifest, std::ios::binary) << text;
    WriteTestReceipt(f.paths, role, manifest, f.paths.build() / (role + ".exe"),
                     {f.paths.inputs() / (role + ".xex")});
  }
  fs::remove(f.paths.inputs() / "observed.txt");
  for (int launch = 0; launch < 2; ++launch) {
    result = f.Run(10);
    INFO(Read(f.root / "err.log"));
    REQUIRE(result.exit_code == 0);
    REQUIRE_FALSE(result.timed_out);
    for (size_t i = 0; i < files.size(); ++i)
      REQUIRE(DigestFile(files[i])->sha256 == hashes[i]);
  }
  std::istringstream observed(Read(f.paths.inputs() / "observed.txt"));
  for (int launch = 0; launch < 2; ++launch) {
    for (unsigned expected = 0; expected < 3; ++expected) {
      unsigned step = 99; bool dashboard = false;
      REQUIRE(bool(observed >> step >> dashboard));
      CHECK(step == expected);
      CHECK(dashboard == (expected != 1));
    }
  }
  std::string extra;
  CHECK_FALSE(bool(observed >> extra));
}
