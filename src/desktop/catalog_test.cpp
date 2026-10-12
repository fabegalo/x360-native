#include <catch2/catch_test_macros.hpp>
#include "catalog.h"
#include "sha256.h"
#include <chrono>
#include <fstream>

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
    target.inputs = {{target.game_root / "dash.xex", "dash.xex", Hash("dashboard")},
                     {target.game_root / "system/xam_ui.xex", "xam.xex", Hash("module")}};
    Write(source / "dash.xex", "dashboard"); Write(source / "system/xam.xex", "module");
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
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
