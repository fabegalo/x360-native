#include <catch2/catch_test_macros.hpp>
#include "session_profile.h"
#include "sha256.h"
#include "scratch.h"
#include <fstream>

namespace fs = std::filesystem;
using namespace x360n;

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
  request.path = file.string(); std::ofstream(file) << "different fixture";
  CHECK_FALSE(MatchesLaunch(profile, request));
  request.kind = LaunchRequest::WindowClose; CHECK_FALSE(MatchesLaunch(profile, request));
  fs::remove_all(dir);
}
