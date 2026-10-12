#include <catch2/catch_test_macros.hpp>
#include "catalog.h"
#include "sha256.h"
#include <chrono>
#include <fstream>
#include <cstdlib>
#include <algorithm>

namespace fs = std::filesystem;
using namespace x360n;
using namespace x360n::desktop;
namespace {
void Write(const fs::path& file, const std::string& value) {
  fs::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary); out << value;
}
std::string Hash(const std::string& value) {
  return Sha256Hex({reinterpret_cast<const uint8_t*>(value.data()), value.size()});
}
struct Fixture {
  fs::path root = fs::temp_directory_path() / fs::path(u8"x360n-desktop-á-") /
    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  ProjectPaths paths{root};
  fs::path source = root / "original contents";
  Target target;
  Fixture() {
    fs::create_directories(source);
    target.id = "dash_test"; target.game_root = paths.inputs() / "dashboard/test";
    target.exe = paths.build() / "titles/dash_test/dash_test.exe";
    target.inputs = {{target.game_root / "dash.xex", "dash.xex", Hash("dashboard"), {}},
                     {target.game_root / "system/xam_ui.xex", "xam.xex", Hash("module"), {}}};
    Write(source / "dash.xex", "dashboard"); Write(source / "system/xam.xex", "module");
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
}

TEST_CASE("asset inventory rejects missing data and same-name files in the wrong folder") {
  Fixture f;
  f.target.inputs.push_back({f.target.game_root / "art/texture.bin", "texture.bin", Hash("texture"), "art/texture.bin"});
  Write(f.source / "other/texture.bin", "texture");
  CHECK_THROWS(Import(f.paths, f.target, f.source));
  CHECK_FALSE(fs::exists(f.target.inputs.front().destination));
  Write(f.source / "art/texture.bin", "texture");
  CHECK(Import(f.paths, f.target, f.source) == 3);
  Write(f.target.inputs.back().destination, "broken");
  CHECK_FALSE(Inspect(f.paths, f.target).inputs_ok);
}

TEST_CASE("packaged environment is retryable and preserves existing sources and saves") {
  Fixture f; auto payload = f.root / "package/workspace"; auto destination = f.root / fs::path(u8"Dados do João 日本");
  Write(payload / "deps.lock.json", "fixture lock");
  Write(payload / "scripts/prepare.ps1", "fixture script");
  Write(payload / "payload.sha256", Hash("fixture lock") + "\tdeps.lock.json\n" + Hash("fixture script") + "\tscripts/prepare.ps1\n");
  InitializeEnvironment(payload, destination);
  Write(destination / ".local/user/save", "progress");
  CHECK_NOTHROW(InitializeEnvironment(payload, destination));
  CHECK(DigestFile(destination / ".local/user/save")->sha256 == Hash("progress"));
  Write(destination / "scripts/prepare.ps1", "preserve local change");
  CHECK_THROWS(InitializeEnvironment(payload, destination));
  CHECK(DigestFile(destination / "scripts/prepare.ps1")->sha256 == Hash("preserve local change"));
}

TEST_CASE("packaged environment rejects tampering and path traversal before writing") {
  Fixture f; auto payload = f.root / "package/workspace"; auto destination = f.root / "empty";
  Write(payload / "deps.lock.json", "different bytes");
  Write(payload / "payload.sha256", Hash("fixture lock") + "\tdeps.lock.json\n");
  CHECK_THROWS(InitializeEnvironment(payload, destination)); CHECK_FALSE(fs::exists(destination));
  Write(payload / "payload.sha256", Hash("different bytes") + "\t../outside\n");
  CHECK_THROWS(InitializeEnvironment(payload, destination)); CHECK_FALSE(fs::exists(destination));
}
TEST_CASE("desktop opens an empty environment without inventing targets") {
  Fixture f;
  CHECK(ReadCatalog(f.paths).empty());
  auto state = Inspect(f.paths, f.target);
  CHECK_FALSE(state.inputs_ok); CHECK_FALSE(state.prepared); CHECK(state.issues.size() == 3);
}
TEST_CASE("import preserves originals and aliases only a matching module") {
  Fixture f;
  REQUIRE(Import(f.paths, f.target, f.source) == 2);
  CHECK(DigestFile(f.source / "system/xam.xex")->sha256 == Hash("module"));
  CHECK(DigestFile(f.target.inputs[1].destination)->sha256 == Hash("module"));
  CHECK(Import(f.paths, f.target, f.source) == 0);
  auto state = Inspect(f.paths, f.target);
  CHECK(state.inputs_ok); CHECK_FALSE(state.prepared);
  Write(f.target.exe, "synthetic build fixture");
  CHECK(Inspect(f.paths, f.target).prepared);
}
TEST_CASE("wrong module version rejects the entire import before writing") {
  Fixture f; Write(f.source / "system/xam.xex", "other version");
  REQUIRE_THROWS(Import(f.paths, f.target, f.source));
  CHECK_FALSE(fs::exists(f.target.inputs[0].destination));
}
TEST_CASE("an existing different destination is never overwritten") {
  Fixture f; Write(f.target.inputs[0].destination, "keep existing");
  REQUIRE_THROWS(Import(f.paths, f.target, f.source));
  CHECK(DigestFile(f.target.inputs[0].destination)->sha256 == Hash("keep existing"));
  CHECK_FALSE(fs::exists(f.target.inputs[1].destination));
}
TEST_CASE("import rejects paths outside inputs and leaves saves intact") {
  Fixture f; auto save = f.paths.local() / "user/save.bin"; Write(save, "progress");
  f.target.game_root = f.paths.local() / "user";
  REQUIRE_THROWS(Import(f.paths, f.target, f.source));
  CHECK(DigestFile(save)->sha256 == Hash("progress"));
}
TEST_CASE("cancelled import is retryable without changing original data") {
  Fixture f;
  REQUIRE_THROWS(Import(f.paths, f.target, f.source, [] { return true; }));
  CHECK_FALSE(fs::exists(f.target.inputs[0].destination));
  CHECK(Import(f.paths, f.target, f.source) == 2);
}
TEST_CASE("modified imported module disables prepared state") {
  Fixture f; Import(f.paths, f.target, f.source); Write(f.target.exe, "build fixture");
  REQUIRE(Inspect(f.paths, f.target).prepared);
  Write(f.target.inputs[1].destination, "wrong module");
  CHECK_FALSE(Inspect(f.paths, f.target).prepared);
}
TEST_CASE("catalog is per target and accepts Unicode workspace paths") {
  Fixture f;
  Write(f.root / "titles/dash_test/dash_test_manifest.toml", R"(
[project]
game_root = "../../.local/inputs/dashboard/test"
)");
  Write(f.root / "titles/dash_test/preparation.toml",
        "label = 'Test'\nlimitations = 'Synthetic fixture'\nsession = true\n"
        "[[files]]\npath = 'dash.xex'\nsource_name = 'dash.xex'\nsha256 = '" + Hash("dashboard") + "'\n");
  auto catalog = ReadCatalog(f.paths);
  REQUIRE(catalog.size() == 1);
  CHECK(catalog[0].session);
  CHECK(catalog[0].game_root == fs::weakly_canonical(f.target.game_root));
  CHECK(Utf8(f.root).find("á") != std::string::npos);
}

TEST_CASE("missing runtime and interrupted build never qualify an old exe as prepared") {
  Fixture f; REQUIRE(Import(f.paths, f.target, f.source) == 2);
  Write(f.target.exe, "old build");
  f.target.runtime_files = {f.target.exe.parent_path() / "runtime.dll"};
  CHECK_FALSE(Inspect(f.paths, f.target).prepared);
  Write(f.target.runtime_files.front(), "runtime fixture");
  REQUIRE(Inspect(f.paths, f.target).prepared);
  Write(PreparationMarker(f.paths, f.target), "incomplete");
  CHECK_FALSE(Inspect(f.paths, f.target).prepared);
  fs::remove(PreparationMarker(f.paths, f.target));
  CHECK(Inspect(f.paths, f.target).prepared);
}

TEST_CASE("local packaged payload imports the complete validated sets into a fresh Unicode environment", "[.local-package]") {
  const char* package = std::getenv("X360N_TEST_PACKAGE");
  const char* original = std::getenv("X360N_TEST_SOURCE");
  REQUIRE(package); REQUIRE(original);
  Fixture f; auto root = f.root / fs::path(u8"Ambiente João 日本");
  InitializeEnvironment(PathFromUtf8(package) / "workspace", root);
  auto targets = ReadCatalog(ProjectPaths{root});
  auto source_targets = ReadCatalog(ProjectPaths{PathFromUtf8(original)});
  REQUIRE(targets.size() >= 3);
  for (const auto& t : targets) {
    if (t.id != "castle_crashers" && t.id != "dash_17559") continue;
    auto it = std::find_if(source_targets.begin(), source_targets.end(), [&](const auto& s) { return s.id == t.id; });
    REQUIRE(it != source_targets.end());
    INFO(t.id);
    CHECK(Import(ProjectPaths{root}, t, it->game_root) == t.inputs.size());
    CHECK(Inspect(ProjectPaths{root}, t).inputs_ok);
    CHECK_FALSE(Inspect(ProjectPaths{root}, t).prepared);
    CHECK(Import(ProjectPaths{root}, t, it->game_root) == 0);
    CHECK(Inspect(ProjectPaths{PathFromUtf8(original)}, *it).inputs_ok);
  }
}
