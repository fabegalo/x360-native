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
