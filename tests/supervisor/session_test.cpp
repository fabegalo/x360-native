#include <catch2/catch_test_macros.hpp>
#include "session_profile.h"
#include "session_progress.h"
#include "sha256.h"
#include "scratch.h"
#include "workspace_lease.h"
#include "preparation_fixture.h"
#include <fstream>

namespace fs = std::filesystem;
using namespace x360n;
TEST_CASE("session diagnostic arguments belong to the selected profile", "[session]") {
  SessionProfile first;
  first.args = {"--other_dashboard_intro=true", "--gpu_plugin=xenos"};
  first.skip_boot_intro_args = {"--other_dashboard_intro=false"};
  CHECK(SessionProfileArguments(first, false) == first.args);
  CHECK(SessionProfileArguments(first, true) == std::vector<std::string>{"--other_dashboard_intro=false", "--gpu_plugin=xenos"});
  SessionProfile second;
  second.args = {"--another_intro=normal"};
  CHECK(SessionProfileArguments(second, true) == second.args);
  CHECK(first.args.front() == "--other_dashboard_intro=true");
}
TEST_CASE("session watchdog resets after sustained execution without a lifetime limit", "[session]") {
  SessionProgress progress;
  for (unsigned cycle = 0; cycle < 1000; ++cycle) {
    for (unsigned step = 0; step < 31; ++step)
      REQUIRE(progress.CanContinue(std::chrono::milliseconds(999)));
    REQUIRE(progress.CanContinue(std::chrono::milliseconds(1000)));
  }
  for (unsigned step = 0; step < 31; ++step)
    REQUIRE(progress.CanContinue(std::chrono::milliseconds(0)));
  CHECK_FALSE(progress.CanContinue(std::chrono::milliseconds(999)));
}
TEST_CASE("selected storage persists without moving or deleting existing saves", "[session]") {
  auto dir = UniqueTempPath("storage"); ProjectPaths paths{dir};
  auto original = paths.local() / "user/dashboard"; fs::create_directories(original);
  std::ofstream(original / "save") << "existing progress";
  auto selected = dir / fs::path(u8"Saves João 日本"); fs::create_directories(selected);
  CHECK(UserDataRoot(paths, original) == original);
  SetUserDataRoot(paths, selected);
  CHECK(UserDataRoot(ProjectPaths{dir}, original) == selected / "dashboard");
  CHECK(fs::exists(original / "save"));
  fs::create_directories(paths.inputs());
  CHECK_THROWS(SetUserDataRoot(paths, paths.inputs()));
  CHECK(UserDataRoot(paths, original) == selected / "dashboard");
  fs::remove_all(dir);
}
TEST_CASE("environment lease rejects concurrent preparation and releases after exit", "[session]") {
  auto dir = UniqueTempPath("lease"); ProjectPaths paths{dir};
  {
    WorkspaceLease first(paths);
    CHECK_THROWS_AS(WorkspaceLease(paths), std::runtime_error);
  }
  CHECK_NOTHROW(WorkspaceLease(paths));
  fs::remove_all(dir);
}

TEST_CASE("session profile and launch request preserve Unicode paths", "[session]") {
  auto dir = UniqueTempPath("unicode") / fs::path(u8"João 日本");
  fs::create_directories(dir / ".local/build"); fs::create_directories(dir / ".local/inputs");
  auto xex = dir / ".local/inputs/default.xex"; std::ofstream(xex) << "test fixture";
  auto exe = dir / ".local/build/test.exe"; std::ofstream(exe) << "synthetic executable";
  auto manifest = dir / "profile.toml";
  std::ofstream(manifest) << "[project]\nname = 'test'\ngame_root = '.local/inputs'\n"
    "[entrypoint]\nfile_path = '.local/inputs/default.xex'\n"
    "[x360n]\nnative_exe = '.local/build/test.exe'\nuser_data_root = '.local/user'\nxex_sha256 = '" << DigestFile(xex)->sha256 << "'\n";
  std::ofstream(dir / "preparation.toml") << "[library]\ntitle_id = '12345678'\ncontent_type = '000D0000'\nfile_name = 'content'\n";
  WriteTestReceipt(ProjectPaths{dir}, "test", manifest, exe, {xex});
  auto profile = LoadSessionProfile(ProjectPaths{dir}, manifest);
  CHECK(profile.exe == fs::canonical(exe));
  CHECK(profile.library_relative_xex == fs::path("0000000000000000/12345678/000D0000/content/default.xex"));
  LaunchRequest request; request.path = PathUtf8(xex);
  CHECK(MatchesLaunch(profile, request));
  CHECK(PathFromUtf8(PathUtf8(dir)) == dir);
  // A title manifest cannot replace the profile authority selected by Session.
  std::ofstream(manifest, std::ios::app) << "skip_boot_intro_args = ['--intro=false']\n";
  CHECK(LoadSessionProfile(ProjectPaths{dir}, manifest, false).skip_boot_intro_args == std::vector<std::string>{"--intro=false"});
  std::ofstream(manifest, std::ios::app) << "args = ['--x360n_profile_root=other-user']\n";
  CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, manifest, false));
  fs::remove_all(dir.parent_path());
}

TEST_CASE("handoff preserves launch bytes and rejects truncated records", "[session]") {
  auto dir = UniqueTempPath("handoff"); fs::create_directories(dir);
  LaunchRequest input;
  input.path = (dir / "default.xex").string(); input.data_present = true;
  input.data = {0, 1, 0xFE, 0xFF}; input.flags = 0x1234;
  auto file = dir / "handoff.bin";
  REQUIRE(WriteLaunchRequest(file, input));
  LaunchRequest output;
  REQUIRE(ReadLaunchRequest(file, output));
  CHECK(output.kind == input.kind); CHECK(output.flags == input.flags);
  CHECK(output.data_present); CHECK(output.path == input.path); CHECK(output.data == input.data);
  fs::resize_file(file, fs::file_size(file) - 1);
  CHECK_FALSE(ReadLaunchRequest(file, output));
  std::ofstream(file, std::ios::binary).write("garbage!", 8);
  CHECK_FALSE(ReadLaunchRequest(file, output));
  fs::remove_all(dir);
}

TEST_CASE("session UI host belongs to its dashboard build and receipt", "[session]") {
  auto dir = UniqueTempPath("session-ui");
  fs::create_directories(dir);
  const auto manifest = dir / "profile.toml";
  const auto setup = dir / "preparation.toml";
  auto write = [&](const std::string& host, const std::string& args = "") {
    std::ofstream(manifest) << "[project]\nname='test'\ngame_root='originals'\n"
      "[entrypoint]\nfile_path='originals/dash.xex'\n[x360n]\n"
      "native_exe='build/dash.exe'\nuser_data_root='user'\nxex_sha256='"
      << std::string(64, '0') << "'\nsystem_ui_host='"
      << host << "'\n" << args;
  };
  write("system_ui.dll");
  CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, manifest, false));
  std::ofstream(setup) << "runtime_files=['system_ui.dll']\n";
  const auto profile = LoadSessionProfile(ProjectPaths{dir}, manifest, false);
  // TEMP may use an 8.3 alias; the loader canonicalizes the existing root.
  CHECK(profile.system_ui_host == fs::canonical(dir) / "build/system_ui.dll");
  for (const auto* host : {"../other-version/system_ui.dll", "C:/other/system_ui.dll", ""}) {
    write(host);
    CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, manifest, false));
  }
  write("system_ui.dll", "args=['--x360n_system_ui_root=another-dashboard']\n");
  CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, manifest, false));
  fs::remove_all(dir);
}

TEST_CASE("session launch requires matching path and exact XEX bytes", "[session]") {
  auto dir = UniqueTempPath("launch-match"); fs::create_directories(dir);
  auto file = dir / "default.xex";
  std::ofstream(file) << "original fixture";
  SessionProfile profile; profile.xex = file; profile.sha256 = DigestFile(file)->sha256;
  LaunchRequest request; request.path = file.string();
  CHECK(MatchesLaunch(profile, request));
  request.flags = 1; CHECK_FALSE(MatchesLaunch(profile, request)); request.flags = 0;
  request.path = (dir / "missing.xex").string(); CHECK_FALSE(MatchesLaunch(profile, request));
  auto other = dir / "other.xex"; fs::copy_file(file, other);
  request.path = other.string(); CHECK_FALSE(MatchesLaunch(profile, request));
  profile.library_relative_xex = "0000000000000000/12345678/000D0000/content/default.xex";
  const auto library = dir / "user/dashboard";
  const auto installed = library / profile.library_relative_xex;
  fs::create_directories(installed.parent_path()); fs::copy_file(file, installed);
  request.path = installed.string();
  CHECK_FALSE(MatchesLaunch(profile, request));
  CHECK(MatchesLaunch(profile, request, library));
  CHECK_FALSE(MatchesLaunch(profile, request, dir / "another-dashboard"));
  request.path = other.string(); CHECK_FALSE(MatchesLaunch(profile, request, library));
  request.path = installed.string(); std::ofstream(installed) << "wrong version";
  CHECK_FALSE(MatchesLaunch(profile, request, library));
  request.path = file.string(); std::ofstream(file) << "different fixture";
  CHECK_FALSE(MatchesLaunch(profile, request));
  request.kind = LaunchRequest::WindowClose; CHECK_FALSE(MatchesLaunch(profile, request));
  fs::remove_all(dir);
}

TEST_CASE("handoff distinguishes title return from closing the platform", "[session]") {
  auto dir = UniqueTempPath("return-handoff"); fs::create_directories(dir);
  auto file = dir / "handoff.bin";
  for (auto kind : {LaunchRequest::Dashboard, LaunchRequest::TitleTerminate,
                    LaunchRequest::GuestExit, LaunchRequest::WindowClose}) {
    LaunchRequest input; input.kind = kind; input.flags = 0x100;
    input.data_present = true; input.data = {0, 0xFF, 3};
    REQUIRE(WriteLaunchRequest(file, input));
    LaunchRequest output;
    REQUIRE(ReadLaunchRequest(file, output));
    CHECK(output.kind == kind); CHECK(output.flags == input.flags);
    CHECK(output.data == input.data);
    fs::remove(file);
  }
  LaunchRequest invalid; invalid.kind = LaunchRequest::Dashboard; invalid.path = "other.xex";
  REQUIRE(WriteLaunchRequest(file, invalid));
  LaunchRequest output; CHECK_FALSE(ReadLaunchRequest(file, output));
  fs::remove_all(dir);
}

TEST_CASE("firmware exit handoff rejects launch data and unsupported routines", "[firmware-exit]") {
  auto dir = UniqueTempPath("firmware-handoff"); fs::create_directories(dir);
  auto file = dir / "handoff.bin";
  for (uint32_t routine : {1u, 5u, 99u}) {
    LaunchRequest input; input.kind = LaunchRequest::FirmwareExit; input.flags = routine;
    REQUIRE(WriteLaunchRequest(file, input));
    LaunchRequest output;
    CHECK(ReadLaunchRequest(file, output) == (routine != 99));
    fs::remove(file);
  }
  LaunchRequest input; input.kind = LaunchRequest::FirmwareExit; input.flags = 5;
  input.data_present = true;
  REQUIRE(WriteLaunchRequest(file, input));
  LaunchRequest output; CHECK_FALSE(ReadLaunchRequest(file, output));
  fs::remove_all(dir);
}

TEST_CASE("session dashboard version comes from its preparation receipt", "[session]") {
  auto dir = UniqueTempPath("session-version");
  fs::create_directories(dir);
  const auto manifest = dir / "profile.toml";
  const auto setup = dir / "preparation.toml";
  auto write = [&](const std::string& args = "") {
    std::ofstream(manifest) << "[project]\nname='test'\ngame_root='originals'\n"
      "[entrypoint]\nfile_path='originals/dash.xex'\n[x360n]\n"
      "native_exe='build/dash.exe'\nuser_data_root='user'\nxex_sha256='"
      << std::string(64, '0') << "'\n" << args;
  };
  write();
  CHECK(LoadSessionProfile(ProjectPaths{dir}, manifest, false).dashboard_version.empty());
  std::ofstream(setup) << "kind='dashboard'\ndashboard_version='2.0.6717.0'\n";
  CHECK(LoadSessionProfile(ProjectPaths{dir}, manifest, false).dashboard_version == "2.0.6717.0");
  std::ofstream(setup) << "kind='dashboard'\ndashboard_version='2.0.6717'\n";
  CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, manifest, false));
  // The supervisor passes the session dashboard's version; profiles cannot.
  std::ofstream(setup) << "kind='dashboard'\ndashboard_version='2.0.17559.0'\n";
  write("args=['--x360n_dashboard_version=2.0.4552.0']\n");
  CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, manifest, false));
  fs::remove_all(dir);
}

TEST_CASE("avatar service files belong to the session dashboard version and bytes", "[session][avatar]") {
  auto dir = UniqueTempPath("session-avatar");
  fs::create_directories(dir / "dash/originals/system");
  fs::create_directories(dir / "dash/originals/avatar");
  fs::create_directories(dir / "game/codegen/content");
  const auto xam = dir / "dash/originals/system/xam_ui.xex";
  const auto catalog = dir / "dash/originals/avatar/AvatarAssetPack.toc";
  std::ofstream(xam, std::ios::binary) << "xam fixture";
  std::ofstream(catalog, std::ios::binary) << "catalog fixture";
  const auto xam_sha = DigestFile(xam)->sha256, catalog_sha = DigestFile(catalog)->sha256;
  const auto dash_manifest = dir / "dash/dash.toml";
  std::ofstream(dash_manifest) << "[project]\nname='dash'\ngame_root='originals'\n"
      "[entrypoint]\nfile_path='originals/dash.xex'\n[x360n]\nnative_exe='build/dash.exe'\n"
      "user_data_root='user'\nsystem_ui_host='session_ui.dll'\nxex_sha256='" << std::string(64, '0') << "'\n";
  auto prepare = [&](const std::string& version, const std::string& avatar) {
    std::ofstream(dir / "dash/preparation.toml")
        << "kind='dashboard'\ndashboard_version='2.0.17559.0'\nruntime_files=['session_ui.dll']\n"
        << "[[files]]\npath='system/xam_ui.xex'\nsource_name='xam.xex'\nsha256='" << xam_sha << "'\n"
        << "[[files]]\npath='avatar/AvatarAssetPack.toc'\nsource_name='AvatarAssetPack.toc'\nsha256='"
        << catalog_sha << "'\noptional=true\n"
        << "[avatar]\nversion='" << version << "'\nxam.path='system/xam_ui.xex'\nxam.provenance='update zip'\n"
        << avatar;
  };
  const std::string catalog_entry = "catalog.path='avatar/AvatarAssetPack.toc'\ncatalog.provenance='storage pack'\n";
  const auto game_manifest = dir / "game/game.toml";
  auto title = [&](const std::string& extra) {
    std::ofstream(game_manifest) << "[project]\nname='game'\ngame_root='codegen'\n"
        "[entrypoint]\nfile_path='codegen/content/default.xex'\n[x360n]\n"
        "game_data_root='codegen/content'\nnative_exe='build/game.exe'\n"
        "user_data_root='user'\nxex_sha256='" << std::string(64, '0') << "'\n" << extra;
    return LoadSessionProfile(ProjectPaths{dir}, game_manifest, false);
  };
  const auto requirement = "avatar_service='2.0.17559.0'\navatar_xam_sha256='" + xam_sha + "'\n";

  prepare("2.0.17559.0", catalog_entry);
  const auto dashboard = LoadSessionProfile(ProjectPaths{dir}, dash_manifest, false);
  REQUIRE(dashboard.avatar_files);
  CHECK(dashboard.avatar_files->catalog.sha256 == catalog_sha);
  auto game = title(requirement);
  // The codegen VFS root may be wider than the title's own content root.
  CHECK(game.game_root == fs::weakly_canonical(dir / "game/codegen/content"));
  CHECK(AvatarServiceArguments(dashboard, game) ==
        std::vector<std::string>{"--x360n_avatar_xam_17559=" + PathUtf8(fs::canonical(xam)),
                                 "--x360n_avatar_catalog_17559=" + PathUtf8(fs::canonical(catalog))});
  // Titles without the service receive nothing, even from a 17559 dashboard.
  CHECK(AvatarServiceArguments(dashboard, title("")).empty());
  // Profiles cannot pass or disable the service themselves.
  for (const auto* arg : {"--x360n_avatar_service=false", "--x360n_avatar_catalog_17559=other.toc"})
    CHECK_THROWS(title(requirement + "args=['" + arg + "']\n"));
  CHECK_THROWS(title("avatar_service='2.0.17559.0'\n"));
  CHECK_THROWS(title("avatar_service='17559'\navatar_xam_sha256='" + xam_sha + "'\n"));

  auto refused = [&](const SessionProfile& from, const SessionProfile& to, const std::string& expected) {
    try {
      AvatarServiceArguments(from, to);
      FAIL("accepted: " << expected);
    } catch (const std::runtime_error& e) {
      CAPTURE(e.what());
      CHECK(std::string(e.what()).find(expected) != std::string::npos);
    }
  };
  // A title built against another XAM, another dashboard version, or no [avatar].
  refused(dashboard, title("avatar_service='2.0.17559.0'\navatar_xam_sha256='" + std::string(64, 'a') + "'\n"),
          "compiled against xam.xex SHA-256 " + std::string(64, 'a'));
  auto other = dashboard;
  other.dashboard_version = "2.0.13604.0";
  refused(other, game, "requires the session dashboard 2.0.17559.0; dash is 2.0.13604.0");
  other = dashboard;
  other.avatar_files.reset();
  refused(other, game, "registers no avatar files");
  other = dashboard;
  other.system_ui_host.clear();
  refused(other, game, "has no system UI host, which provides the shared XAM image");
  // Missing or different bytes, named with version, file and registered hash.
  fs::rename(catalog, dir / "moved.toc");
  refused(dashboard, game, "missing AvatarAssetPack.toc 2.0.17559.0 (SHA-256 " + catalog_sha + "; storage pack)");
  fs::rename(dir / "moved.toc", catalog);
  std::ofstream(xam, std::ios::binary) << "xam of another version";
  refused(dashboard, game, "different file instead of xam_ui.xex 2.0.17559.0");
  std::ofstream(xam, std::ios::binary) << "xam fixture";
  CHECK(AvatarServiceArguments(dashboard, game).size() == 2);

  // The registration itself must be version-consistent, hashed and inside the root.
  prepare("2.0.13604.0", catalog_entry);
  CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, dash_manifest, false));
  prepare("2.0.17559.0", "catalog.path='avatar/Unregistered.toc'\ncatalog.provenance='x'\n");
  CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, dash_manifest, false));
  prepare("2.0.17559.0", "catalog.path='../../AvatarAssetPack.toc'\ncatalog.provenance='x'\n");
  CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, dash_manifest, false));
  prepare("2.0.17559.0", "catalog.path='avatar/AvatarAssetPack.toc'\n");
  CHECK_THROWS(LoadSessionProfile(ProjectPaths{dir}, dash_manifest, false));
  fs::remove_all(dir);
}
