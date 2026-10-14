#include <catch2/catch_test_macros.hpp>
#include "scratch.h"
#include "sha256.h"
#include "../../titles/common/dashboard_assets.h"
#include "../../titles/dash_17559/src/original_assets.h"
#include "../../titles/dash_6717/src/original_assets.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <map>

using namespace x360n;
namespace fs = std::filesystem;
namespace {
struct Scratch {
  fs::path dir = UniqueTempPath("dashboard_assets");
  Scratch() { fs::create_directories(dir); }
  ~Scratch() { std::error_code ec; fs::remove_all(dir, ec); }
};
void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}
}
TEST_CASE("Dashboard assets reject mixed versions missing modules and stale hash caches", "[dashboard-assets]") {
  Scratch s;
  const auto newer = s.dir / "17559", older = s.dir / "13604";
  Write(newer / "vk.xex", "keyboard 17559");
  Write(older / "vk.xex", "keyboard 13604");
  const auto hash = DigestFile(newer / "vk.xex")->sha256;
  const DashboardAsset assets[] = {{"vk.xex", hash}};
  CHECK(VerifyDashboardAssets(newer, "17559", assets).empty());
  CHECK(VerifyDashboardAssets(older, "17559", assets).find("incompatible SHA-256") != std::string::npos);
  const auto timestamp = fs::last_write_time(newer / "vk.xex");
  Write(newer / "vk.xex", "keyboard 13604"); // same name and size, preserve mtime
  fs::last_write_time(newer / "vk.xex", timestamp);
  CHECK_FALSE(VerifyDashboardAssets(newer, "17559", assets).empty());
  fs::remove(newer / "vk.xex");
  const auto missing = VerifyDashboardAssets(newer, "17559", assets);
  CHECK(missing.find("Retail 17559") != std::string::npos);
  CHECK(missing.find("vk.xex is missing") != std::string::npos);
  CHECK(missing.find(hash) != std::string::npos);
  const DashboardAsset escaped[] = {{"../13604/vk.xex", hash}};
  CHECK(VerifyDashboardAssets(newer, "17559", escaped).find("outside") != std::string::npos);
  const DashboardAsset unknown[] = {{"XenonJKLatin.xtt", ""}};
  CHECK(VerifyDashboardAssets(older, "6717", unknown).find("no verified version/hash") != std::string::npos);
  CHECK(DashboardCacheRoot(s.dir, "17559") != DashboardCacheRoot(s.dir, "13604"));
  CHECK(DashboardCacheRoot(s.dir, "17559") != DashboardCacheRoot(s.dir, "6717"));
}

TEST_CASE("Dashboard assets match preparation and every AOT module stays inside its version", "[dashboard-assets]") {
  const auto root = fs::path(X360N_SOURCE_ROOT);
  for (const std::string version : {"17559", "6717"}) {
    const auto folder = root / "titles" / ("dash_" + version);
    auto info = toml::parse_file((folder / "preparation.toml").string());
    REQUIRE(info["dashboard_version"].value_or("") == "2.0." + version + ".0");
    const std::span<const DashboardAsset> assets = version == "17559"
        ? std::span<const DashboardAsset>(dash17559::kOriginalAssets)
        : std::span<const DashboardAsset>(dash6717::kOriginalAssets);
    std::map<std::string, std::string> expected;
    for (const auto& entry : *info["files"].as_array()) {
      REQUIRE(entry.is_table());
      expected.emplace((*entry.as_table())["path"].value_or(""), (*entry.as_table())["sha256"].value_or(""));
    }
    if (auto pending = info["unverified_dependencies"].as_array())
      for (const auto& entry : *pending) expected.emplace(entry.value_or(""), "");
    REQUIRE(assets.size() == expected.size());
    for (const auto& asset : assets) {
      REQUIRE(expected.contains(std::string(asset.path)));
      CHECK(asset.sha256 == expected.at(std::string(asset.path)));
    }
    const auto main = toml::parse_file((folder / ("dash_" + version + "_manifest.toml")).string());
    const auto game = fs::weakly_canonical(folder / main["project"]["game_root"].value_or(""));
    for (const auto& file : fs::recursive_directory_iterator(folder)) {
      if (!file.is_regular_file() || !file.path().filename().string().ends_with("_manifest.toml") ||
          file.path().string().find("generated") != std::string::npos) continue;
      const auto m = toml::parse_file(file.path().string());
      const auto parent = file.path().parent_path();
      REQUIRE(fs::weakly_canonical(parent / m["project"]["game_root"].value_or("")) == game);
      auto check = [&](const toml::table& node) {
        auto resolved = fs::weakly_canonical(parent / node["file_path"].value_or<std::string>(""));
        const auto rel = resolved.lexically_relative(game);
        REQUIRE_FALSE(rel.empty());
        REQUIRE_FALSE(rel.is_absolute());
        REQUIRE(*rel.begin() != "..");
        CHECK(expected.contains(rel.generic_string()));
      };
      REQUIRE(m["entrypoint"].is_table());
      check(*m["entrypoint"].as_table());
      if (auto modules = m["modules"].as_array()) for (const auto& module : *modules) {
        REQUIRE(module.is_table());
        check(*module.as_table());
      }
    }
  }
}
