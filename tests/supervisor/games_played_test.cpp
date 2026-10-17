#include "../../titles/common/games_played.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <toml++/toml.hpp>

using namespace x360n;
namespace fs = std::filesystem;

namespace {
std::string Hex(uint32_t v) { char s[9]; std::snprintf(s, sizeof s, "%08X", unsigned(v)); return s; }
// The installed layout and 0x148-byte aggregate header the importer writes
// (src/desktop/model/library.cpp); a license mask trailer is optional.
fs::path Install(const fs::path& root, uint32_t title, uint32_t type,
                 std::optional<uint32_t> license) {
  const auto title_dir = root / "0000000000000000" / Hex(title);
  const auto type_name = Hex(type);
  const auto image = title_dir / type_name / "PACKAGE" / "default.xex";
  fs::create_directories(image.parent_path());
  std::ofstream(image) << "image";
  fs::create_directories(title_dir / "Headers" / type_name);
  std::string bytes(0x148, '\0');
  auto be32 = [&](size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes[at + i] = char(v >> (24 - i * 8));
  };
  be32(0, 1);
  be32(4, type);
  be32(0x140, title);
  if (license)
    for (int i = 0; i < 4; ++i) bytes.push_back(char(*license >> (8 * i)));
  std::ofstream(title_dir / "Headers" / type_name / "PACKAGE.header", std::ios::binary) << bytes;
  return image;
}
DashboardVersion Version(const fs::path& preparation) {
  const auto table = toml::parse_file(preparation.string());
  const auto version = ParseDashboardVersion(table["dashboard_version"].value_or(std::string{}));
  REQUIRE(version);
  return *version;
}
}  // namespace

TEST_CASE("dashboard versions parse exactly", "[games-played]") {
  CHECK(ParseDashboardVersion("2.0.17559.0") == DashboardVersion{2, 0, 17559, 0});
  CHECK(ParseDashboardVersion("2.0.6683.0") == kTrialExcludedFrom);
  for (const auto* bad : {"", "2.0.6683", "2.0.6683.0.1", "2.0..0", "2.0.x.0", "2.0.6683.0 "})
    CHECK_FALSE(ParseDashboardVersion(bad));
}

TEST_CASE("Arcade trials join Games Played only before dashboard 2.0.6683.0", "[games-played]") {
  const fs::path root = X360N_SOURCE_ROOT;
  const auto v6717 = Version(root / "titles/dash_6717/preparation.toml");
  const auto v17559 = Version(root / "titles/dash_17559/preparation.toml");
  const auto store = fs::temp_directory_path() /
      ("x360n-games-played-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  constexpr uint32_t castle = 0x584108B7, forza = 0x4D5309C9;
  const auto trial = ReadLaunchedContent(Install(store / "a", castle, kArcadeContentType, std::nullopt), castle);
  const auto unlicensed = ReadLaunchedContent(Install(store / "b", castle, kArcadeContentType, 0u), castle);
  const auto full = ReadLaunchedContent(Install(store / "c", castle, kArcadeContentType, 1u), castle);
  const auto disc = ReadLaunchedContent(Install(store / "d", forza, 0x00004000, std::nullopt), forza);
  REQUIRE(trial);
  REQUIRE(unlicensed);
  REQUIRE(full);
  REQUIRE(disc);
  CHECK(trial->license_mask == 0);
  CHECK(full->license_mask == 1);
  // The header must belong to the running title.
  CHECK_FALSE(ReadLaunchedContent(Install(store / "e", castle, kArcadeContentType, 1u), forza));
  CHECK_FALSE(ReadLaunchedContent(store / "missing" / "default.xex", castle));

  using D = GamesPlayedDecision;
  for (const auto& version : {v6717, v17559, kTrialExcludedFrom}) {
    CHECK(DecideGamesPlayed(version, trial) == D::SkipArcadeTrial);
    CHECK(DecideGamesPlayed(version, unlicensed) == D::SkipArcadeTrial);
    CHECK(DecideGamesPlayed(version, full) == D::Record);
    CHECK(DecideGamesPlayed(version, trial, 1) == D::Record);
    CHECK(DecideGamesPlayed(version, disc) == D::Record);
    CHECK(DecideGamesPlayed(version, std::nullopt) == D::Record);
  }
  // Earlier dashboards keep recording trials.
  for (const DashboardVersion earlier : {DashboardVersion{2, 0, 6682, 0}, DashboardVersion{2, 0, 4552, 0}})
    CHECK(DecideGamesPlayed(earlier, trial) == D::Record);
  CHECK(DecideGamesPlayed(std::nullopt, trial) == D::UnknownDashboard);
  CHECK(DecideGamesPlayed(std::nullopt, full) == D::Record);
  fs::remove_all(store);
}

TEST_CASE("Arcade metadata failures cannot grant a full license", "[games-played]") {
  const auto store = fs::temp_directory_path() /
      ("x360n-license-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto image = Install(store, 0x12345678, kArcadeContentType, 1);
  const auto header = image.parent_path().parent_path().parent_path() /
      "Headers/000D0000/PACKAGE.header";
  REQUIRE(ReadLaunchedContent(image.parent_path() / "bin/default.xex", 0x12345678)->license_mask == 1);
  for (const auto size : {0u, 0x147u, 0x14Bu}) {
    fs::resize_file(header, size);
    const auto content = ReadLaunchedContent(image, 0x12345678);
    REQUIRE(content);
    CHECK(DecideGamesPlayed(kTrialExcludedFrom, content) == GamesPlayedDecision::SkipArcadeTrial);
  }
  fs::remove(header);
  const auto content = ReadLaunchedContent(image, 0x12345678);
  REQUIRE(content);
  CHECK(DecideGamesPlayed(kTrialExcludedFrom, content) == GamesPlayedDecision::SkipArcadeTrial);
  CHECK(DecideGamesPlayed(DashboardVersion{2, 0, 6682, 0}, content) == GamesPlayedDecision::Record);
  fs::remove_all(store);
}
