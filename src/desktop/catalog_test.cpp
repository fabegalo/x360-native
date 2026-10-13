#include <catch2/catch_test_macros.hpp>
#include "catalog.h"
#include "sha256.h"
#include "session_profile.h"
#include "../../tests/supervisor/preparation_fixture.h"
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
  void Receipt() {
    target.manifest = root / "titles/dash_test/dash_test_manifest.toml";
    Write(target.manifest, "[project]\nname='dash_test'\n[entrypoint]\nfile_path='../../.local/inputs/dashboard/test/dash.xex'\n");
    WriteTestReceipt(paths, target.id, target.manifest, target.exe,
        {target.inputs[0].destination, target.inputs[1].destination});
  }
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
TEST_CASE("individual renamed components are identified by bytes and imported incrementally") {
  Fixture f;
  auto file = f.source / "renamed.bin"; Write(file, "dashboard");
  auto identity = IdentifyContent({f.target}, file);
  REQUIRE(identity.size() == 1); CHECK(identity[0].recognized); CHECK(identity[0].target_id == f.target.id);
  CHECK(Import(f.paths, f.target, file, {}, true) == 1);
  CHECK_FALSE(Inspect(f.paths, f.target).inputs_ok);
  CHECK(Import(f.paths, f.target, f.source / "system/xam.xex", {}, true) == 1);
  CHECK(Inspect(f.paths, f.target).inputs_ok);
  CHECK(Import(f.paths, f.target, file, {}, true) == 0);
  CHECK(DigestFile(file)->sha256 == Hash("dashboard"));
}
TEST_CASE("unsupported and corrupt files are not copied or accepted as compatible") {
  Fixture f; auto file = f.source / "unknown.xex";
  Write(file, "XEX1older header");
  auto old = IdentifyContent({f.target}, file);
  REQUIRE(old.size() == 1); CHECK_FALSE(old[0].recognized); CHECK(old[0].label == "Formato antigo do Xbox 360");
  Write(file, "XEX2bad");
  auto bad = IdentifyContent({f.target}, file);
  CHECK_FALSE(bad[0].recognized); CHECK(bad[0].label == "Arquivo incompleto ou inválido");
  CHECK_FALSE(fs::exists(f.target.game_root));
}
TEST_CASE("optional games do not prevent an empty dashboard library") {
  Fixture f; Target session; session.session = true; session.library_targets = {f.target.id};
  session.user_root = f.paths.local() / "user/dashboard";
  CHECK(InspectLibrary(session, {f.target}).empty());
  CHECK_NOTHROW(PrepareLibrary(f.paths, session, {f.target}));
  CHECK_FALSE(fs::exists(session.user_root));
}
TEST_CASE("catalog routing can describe an absent game but execution still rejects it") {
  Fixture f; auto manifest = f.root / "profile.toml";
  Write(manifest, "[project]\nname='sample'\ngame_root='.local/inputs/game'\n[entrypoint]\nfile_path='.local/inputs/game/default.xex'\n[x360n]\nnative_exe='.local/build/game.exe'\nuser_data_root='.local/user'\nxex_sha256='" + Hash("game") + "'\n");
  auto profile = LoadSessionProfile(f.paths, manifest, false);
  CHECK(profile.name == "sample");
  CHECK_THROWS(LoadSessionProfile(f.paths, manifest));
  LaunchRequest request; request.path = Utf8(profile.xex); CHECK_FALSE(MatchesLaunch(profile, request));
  Write(profile.xex, "game"); CHECK(MatchesLaunch(profile, request));
  CHECK_THROWS(LoadSessionProfile(f.paths, manifest));
  Write(profile.exe, "synthetic executable");
  CHECK_THROWS(LoadSessionProfile(f.paths, manifest));
  WriteTestReceipt(f.paths, "sample", manifest, profile.exe, {profile.xex});
  CHECK_NOTHROW(LoadSessionProfile(f.paths, manifest));
  Write(profile.xex, "changed game"); CHECK_THROWS(LoadSessionProfile(f.paths, manifest));
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
TEST_CASE("interrupted source payload resumes without discarding partial bytes or saves") {
  Fixture f; auto payload = f.root / "package/workspace"; auto destination = f.root / "environment";
  Write(payload / "deps.lock.json", "fixture lock");
  Write(payload / "payload.sha256", Hash("fixture lock") + "\tdeps.lock.json\n");
  Write(destination / "deps.lock.json.x360n-tmp", "interrupted bytes");
  Write(destination / ".local/user/save", "progress");
  InitializeEnvironment(payload, destination);
  CHECK(DigestFile(destination / "deps.lock.json")->sha256 == Hash("fixture lock"));
  CHECK(DigestFile(destination / ".local/user/save")->sha256 == Hash("progress"));
  CHECK(DigestFile(destination / "deps.lock.json.x360n-tmp")->sha256 == Hash("interrupted bytes"));
}

TEST_CASE("payload update resumes after interruption and preserves content and original backups") {
  Fixture f; auto payload = f.root / "package/workspace", destination = f.root / "environment";
  auto publish = [&](const std::string& version) {
    Write(payload / "deps.lock.json", "fixture lock");
    Write(payload / "titles/one/source.cpp", version);
    Write(payload / "scripts/prepare.ps1", version);
    Write(payload / "payload.sha256", Hash("fixture lock") + "\tdeps.lock.json\n" + Hash(version) + "\ttitles/one/source.cpp\n" + Hash(version) + "\tscripts/prepare.ps1\n");
  };
  publish("old"); InitializeEnvironment(payload, destination);
  for (auto rel : {".local/inputs/game/default.xex", ".local/inputs/dashboard/dash.xex", ".local/user/profile", ".local/user/save"}) Write(destination / rel, "keep");
  publish("new");
  CHECK_FALSE(InspectEnvironment(payload, destination).current);
  CHECK_THROWS(InitializeEnvironment(payload, destination));
  CHECK_THROWS(InitializeEnvironment(payload, destination, true, [] { throw std::runtime_error("power loss"); }));
  CHECK(InspectEnvironment(payload, destination).interrupted);
  CHECK_FALSE(InspectEnvironment(payload, destination).current);
  CHECK_THROWS(InitializeEnvironment(payload, destination));
  CHECK_NOTHROW(InitializeEnvironment(payload, destination, true));
  CHECK(InspectEnvironment(payload, destination).current);
  CHECK(DigestFile(destination / ".local/payload-update/backup/scripts/prepare.ps1")->sha256 == Hash("old"));
  CHECK(DigestFile(destination / "titles/one/source.cpp")->sha256 == Hash("new"));
  for (auto rel : {".local/inputs/game/default.xex", ".local/inputs/dashboard/dash.xex", ".local/user/profile", ".local/user/save"}) CHECK(DigestFile(destination / rel)->sha256 == Hash("keep"));
  const auto time = fs::last_write_time(destination / "titles/one/source.cpp");
  InitializeEnvironment(payload, destination);
  CHECK(fs::last_write_time(destination / "titles/one/source.cpp") == time);
}

TEST_CASE("source update relies on C2 receipts and invalidates only affected preparations") {
  Fixture f; Import(f.paths, f.target, f.source); Write(f.target.exe, "fixture exe"); f.Receipt();
  Write(f.root / "titles/one.cpp", "old");
  WriteTestReceipt(f.paths, f.target.id, f.target.manifest, f.target.exe,
    {f.target.inputs[0].destination, f.target.inputs[1].destination, f.root / "titles/one.cpp"});
  auto other = f.target.exe.parent_path() / "other.exe", manifest = f.root / "titles/other_manifest.toml";
  Write(other, "independent exe"); Write(manifest, "[entrypoint]\nfile_path='../.local/inputs/dashboard/test/dash.xex'\n");
  WriteTestReceipt(f.paths, "other", manifest, other, {f.target.inputs[0].destination});
  auto payload = f.root / "package/workspace";
  Write(payload / "deps.lock.json", "fixture lock"); Write(payload / "titles/one.cpp", "new");
  auto manifestIndex = std::string{};
  for (const auto& file : {f.target.manifest, manifest}) {
    const auto rel = file.lexically_relative(f.root);
    fs::create_directories((payload / rel).parent_path()); fs::copy_file(file, payload / rel);
    manifestIndex += DigestFile(file)->sha256 + "\t" + rel.generic_string() + "\n";
  }
  Write(payload / "payload.sha256", Hash("fixture lock") + "\tdeps.lock.json\n" + Hash("new") + "\ttitles/one.cpp\n" + manifestIndex);
  REQUIRE(Inspect(f.paths, f.target).prepared);
  REQUIRE_FALSE(CheckPreparation(f.paths, "other", manifest, other));
  CHECK_THROWS(InitializeEnvironment(payload, f.root, true, [] { throw std::runtime_error("power loss"); }));
  CHECK_FALSE(Inspect(f.paths, f.target).prepared);
  CHECK(CheckPreparation(f.paths, "other", manifest, other)); // pending blocks even valid receipts
  InitializeEnvironment(payload, f.root, true);
  CHECK_FALSE(Inspect(f.paths, f.target).prepared);
  CHECK_FALSE(CheckPreparation(f.paths, "other", manifest, other));
  CHECK(fs::exists(PreparationReceipt(f.target.exe))); // Never delete or re-sign a stale receipt.
}

TEST_CASE("package identity rejects swapped binaries missing payload and foreign DLLs") {
  Fixture f; auto package = f.root / "distribution";
  Write(package / "x360-native.exe", "frontend"); Write(package / "app/x360n.exe", "supervisor");
  Write(package / "package.json", "metadata");
  Write(package / "app/workspace/deps.lock.json", "lock");
  auto payloadIndex = Hash("lock") + "\tdeps.lock.json\n";
  Write(package / "app/workspace/payload.sha256", payloadIndex);
  Write(package / "package.sha256", Hash("frontend") + "\tx360-native.exe\n" + Hash("supervisor") + "\tapp/x360n.exe\n" + Hash("metadata") + "\tpackage.json\n" + Hash(payloadIndex) + "\tapp/workspace/payload.sha256\n" + Hash("lock") + "\tapp/workspace/deps.lock.json\n");
  REQUIRE_NOTHROW(VerifyPackage(package));
  CHECK_NOTHROW(VerifyPackage(package, Hash(payloadIndex)));
  CHECK_THROWS(VerifyPackage(package, Hash("another source revision")));
  SECTION("foreign DLL") { Write(package / "app/fmt.dll", "incompatible"); CHECK_THROWS(VerifyPackage(package)); }
  SECTION("swapped supervisor") { Write(package / "app/x360n.exe", "other version"); CHECK_THROWS(VerifyPackage(package)); }
  SECTION("swapped frontend") { Write(package / "x360-native.exe", "other version"); CHECK_THROWS(VerifyPackage(package)); }
  SECTION("incomplete payload") { fs::remove(package / "app/workspace/deps.lock.json"); CHECK_THROWS(VerifyPackage(package)); }
  SECTION("missing index") { fs::remove(package / "package.sha256"); CHECK_THROWS(VerifyPackage(package)); }
}

TEST_CASE("retired managed files and local edits are preserved in update backups") {
  Fixture f; auto payload = f.root / "package/workspace", destination = f.root / "environment";
  Write(payload / "deps.lock.json", "lock"); Write(payload / "obsolete.cpp", "old");
  Write(payload / "payload.sha256", Hash("lock") + "\tdeps.lock.json\n" + Hash("old") + "\tobsolete.cpp\n");
  InitializeEnvironment(payload, destination);
  Write(destination / "obsolete.cpp", "local edit");
  Write(payload / "payload.sha256", Hash("lock") + "\tdeps.lock.json\n");
  CHECK_FALSE(InspectEnvironment(payload, destination).current);
  InitializeEnvironment(payload, destination, true);
  CHECK_FALSE(fs::exists(destination / "obsolete.cpp"));
  CHECK(DigestFile(destination / ".local/payload-update/backup/obsolete.cpp")->sha256 == Hash("local edit"));
  CHECK(InspectEnvironment(payload, destination).current);
}

TEST_CASE("legacy environment archives obsolete sources but keeps generated AOT and user data") {
  Fixture f; auto payload = f.root / "package/workspace", destination = f.root / "environment";
  Write(payload / "deps.lock.json", "lock"); Write(payload / "payload.sha256", Hash("lock") + "\tdeps.lock.json\n");
  Write(destination / "deps.lock.json", "lock");
  Write(destination / "src/old.cpp", "obsolete source");
  Write(destination / "titles/game/generated/game.0.cpp", "preserved AOT");
  const auto generatedTime = fs::last_write_time(destination / "titles/game/generated/game.0.cpp");
  REQUIRE_FALSE(InspectEnvironment(payload, destination).current);
  InitializeEnvironment(payload, destination, true);
  CHECK(InspectEnvironment(payload, destination).current);
  CHECK_FALSE(fs::exists(destination / "src/old.cpp"));
  CHECK(DigestFile(destination / ".local/payload-update/backup/src/old.cpp")->sha256 == Hash("obsolete source"));
  CHECK(fs::last_write_time(destination / "titles/game/generated/game.0.cpp") == generatedTime);
}

TEST_CASE("real distribution updates a previous source payload and preserves imported sets", "[.local-update]") {
  const auto package = std::getenv("X360N_TEST_PACKAGE"), old = std::getenv("X360N_TEST_OLD_PAYLOAD"), original = std::getenv("X360N_TEST_SOURCE");
  REQUIRE(package); REQUIRE(old); REQUIRE(original);
  Fixture f; auto destination = f.root / fs::path(u8"Atualização João 日本");
  const auto payload = PathFromUtf8(package) / "app/workspace";
  VerifyPackage(PathFromUtf8(package));
  InitializeEnvironment(PathFromUtf8(old), destination);
  const auto before = ReadCatalog(ProjectPaths{destination});
  const auto originals = ReadCatalog(ProjectPaths{PathFromUtf8(original)});
  for (auto id : {"castle_crashers", "dash_17559"}) {
    auto target = std::find_if(before.begin(), before.end(), [&](const auto& t) { return t.id == id; });
    auto source = std::find_if(originals.begin(), originals.end(), [&](const auto& t) { return t.id == id; });
    REQUIRE(target != before.end()); REQUIRE(source != originals.end());
    REQUIRE(Import(ProjectPaths{destination}, *target, source->game_root) == target->inputs.size());
  }
  for (auto rel : {".local/user/profile", ".local/user/save"}) Write(destination / rel, "preserve user bytes");
  CHECK_FALSE(InspectEnvironment(payload, destination).current);
  CHECK_THROWS(InitializeEnvironment(payload, destination));
  CHECK_THROWS(InitializeEnvironment(payload, destination, true, [] { throw std::runtime_error("interrupted update"); }));
  CHECK_FALSE(InspectEnvironment(payload, destination).current);
  InitializeEnvironment(payload, destination, true);
  CHECK(InspectEnvironment(payload, destination).current);
  for (const auto& t : ReadCatalog(ProjectPaths{destination})) {
    if (t.id != "castle_crashers" && t.id != "dash_17559") continue;
    CHECK(Inspect(ProjectPaths{destination}, t).inputs_ok);
    CHECK_FALSE(Inspect(ProjectPaths{destination}, t).prepared);
  }
  for (auto rel : {".local/user/profile", ".local/user/save"}) CHECK(DigestFile(destination / rel)->sha256 == Hash("preserve user bytes"));
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
  CHECK_FALSE(Inspect(f.paths, f.target).prepared);
  f.Receipt();
  CHECK(Inspect(f.paths, f.target).prepared);
}
TEST_CASE("wrong module version rejects the entire import before writing") {
  Fixture f; Write(f.source / "system/xam.xex", "other version");
  REQUIRE_THROWS(Import(f.paths, f.target, f.source));
  CHECK_FALSE(fs::exists(f.target.inputs[0].destination));
}
TEST_CASE("import detects the exact target and reports a mixed folder as ambiguous") {
  Fixture f; auto older = f.target; older.id = "older"; older.inputs.front().sha256 = Hash("older dashboard");
  auto ids = DetectImportTargets({f.target, older}, f.source);
  REQUIRE(ids.size() == 1); CHECK(ids.front() == f.target.id);
  Write(f.source / "older/dash.xex", "older dashboard");
  CHECK(DetectImportTargets({f.target, older}, f.source).size() == 2);
  CHECK_FALSE(fs::exists(f.target.inputs.front().destination));
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
TEST_CASE("interrupted copy is preserved and import resumes with verified bytes") {
  Fixture f;
  auto staging = f.target.game_root / ".x360n-import-dash.xex";
  Write(staging / "verified.tmp", "partial bytes");
  CHECK(Import(f.paths, f.target, f.source) == 2);
  CHECK(DigestFile(f.target.inputs.front().destination)->sha256 == Hash("dashboard"));
  bool preserved = false;
  for (auto& entry : fs::directory_iterator(f.target.game_root))
    if (Utf8(entry.path().filename()).starts_with(".x360n-import-dash.xex.interrupted-"))
      preserved = DigestFile(entry.path() / "verified.tmp")->sha256 == Hash("partial bytes");
  CHECK(preserved);
}
TEST_CASE("modified imported module disables prepared state") {
  Fixture f; Import(f.paths, f.target, f.source); Write(f.target.exe, "build fixture");
  f.Receipt();
  REQUIRE(Inspect(f.paths, f.target).prepared);
  Write(f.target.inputs[1].destination, "wrong module");
  CHECK_FALSE(Inspect(f.paths, f.target).prepared);
}
TEST_CASE("catalog requires the receipt and rejects replaced native artifacts") {
  Fixture f; Import(f.paths, f.target, f.source); Write(f.target.exe, "build fixture");
  CHECK_FALSE(Inspect(f.paths, f.target).prepared);
  f.Receipt(); REQUIRE(Inspect(f.paths, f.target).prepared);
  Write(f.target.exe, "other version");
  auto state = Inspect(f.paths, f.target);
  CHECK(state.inputs_ok); CHECK_FALSE(state.prepared);
  REQUIRE_FALSE(state.issues.empty()); CHECK(state.issues.front().find("prepare novamente") != std::string::npos);
}
TEST_CASE("local receipts keep Castle Crashers dashboard and Iruka ready", "[.preparation-local]") {
  const char* root = std::getenv("X360N_TEST_PREPARED_ROOT"); REQUIRE(root);
  ProjectPaths paths{PathFromUtf8(root)};
  auto targets = ReadCatalog(paths);
  const char* alternate = std::getenv("X360N_TEST_PREPARED_BUILDS");
  const auto builds = alternate ? PathFromUtf8(alternate) : paths.build() / "titles";
  for (auto id : {"castle_crashers", "dash_17559"}) {
    CAPTURE(id);
    const auto target = std::find_if(targets.begin(), targets.end(), [&](const auto& t) { return t.id == id; });
    REQUIRE(target != targets.end());
    auto selected = *target;
    selected.exe = builds / id / (std::string(id) + ".exe");
    for (auto& file : selected.runtime_files) file = selected.exe.parent_path() / file.filename();
    auto state = Inspect(paths, selected);
    for (const auto& issue : state.issues) INFO(issue);
    CHECK(state.inputs_ok); CHECK(state.prepared);
  }
  CHECK_FALSE(CheckPreparation(paths, "iruka", paths.root / "titles/iruka/iruka_manifest.toml",
      builds / "iruka/iruka.exe"));
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
  f.Receipt();
  REQUIRE(Inspect(f.paths, f.target).prepared);
  Write(PreparationMarker(f.paths, f.target), "incomplete");
  CHECK_FALSE(Inspect(f.paths, f.target).prepared);
  fs::remove(PreparationMarker(f.paths, f.target));
  CHECK(Inspect(f.paths, f.target).prepared);
}

TEST_CASE("original library registration validates all bytes and publishes the header last") {
  Fixture f; Import(f.paths, f.target, f.source);
  auto game = f.target;
  game.library = {"584108B7", "000D0000", "F9432A7FE407A3C196C95D0FFD4A540937FD4EEC58", "Castle Crashers", Hash("package provenance")};
  Target session; session.session = true; session.user_root = f.paths.local() / "user/dashboard";
  session.dependencies = {game.id};
  session.inputs = game.inputs;
  CHECK_FALSE(InspectLibrary(game, {game, session}).empty());
  auto base = session.user_root / "0000000000000000/584108B7";
  auto header = base / "Headers/000D0000" / (game.library.file_name + ".header");
  auto installed = base / "000D0000" / game.library.file_name;
  CHECK_FALSE(InspectLibrary(session, {game}).empty());
  Write(installed / "dash.xex.x360n-library-tmp", "interrupted");
  CHECK_THROWS(PrepareLibrary(f.paths, session, {game}, [] { return true; }));
  CHECK_FALSE(fs::exists(header));
  PrepareLibrary(f.paths, session, {game});
  CHECK(InspectLibrary(session, {game}).empty());
  CHECK(InspectLibrary(game, {game, session}).empty());
  // Exact reference generated for the original LIVE metadata in the working
  // dashboard, independent of these synthetic fixture content bytes.
  CHECK(DigestFile(header)->sha256 == "3b5f5f95911839ccd0227af47c742dac659e2542d4d141cf9854ed4d37063b7c");
  CHECK(fs::file_size(header) == 0x148); // no invented license trailer
  CHECK_NOTHROW(PrepareLibrary(f.paths, session, {game}));
  Write(installed / "dash.xex", "preserve installed data");
  CHECK_THROWS(PrepareLibrary(f.paths, session, {game}));
  CHECK(DigestFile(installed / "dash.xex")->sha256 == Hash("preserve installed data"));
  CHECK_FALSE(InspectLibrary(session, {game}).empty());
  CHECK_FALSE(InspectLibrary(game, {game, session}).empty());
}

TEST_CASE("original library supports long content paths without Windows policy changes") {
  Fixture f; Import(f.paths, f.target, f.source);
  auto game = f.target;
  game.library = {"584108B7", "000D0000", "F9432A7FE407A3C196C95D0FFD4A540937FD4EEC58", "Castle Crashers", Hash("package provenance")};
  Target session; session.session = true;
  session.user_root = f.root / std::string(110, 'x');
  session.dependencies = {game.id};
  REQUIRE((session.user_root / "0000000000000000/584108B7/000D0000" / game.library.file_name / "system/xam_ui.xex.x360n-library-tmp").native().size() > 260);
  REQUIRE_NOTHROW(PrepareLibrary(f.paths, session, {game}));
  CHECK(InspectLibrary(session, {game}).empty());
  CHECK_NOTHROW(PrepareLibrary(f.paths, session, {game}));
}

TEST_CASE("local packaged payload imports the complete validated sets into a fresh Unicode environment", "[.local-package]") {
  const char* package = std::getenv("X360N_TEST_PACKAGE");
  const char* original = std::getenv("X360N_TEST_SOURCE");
  REQUIRE(package); REQUIRE(original);
  Fixture f; auto root = f.root / fs::path(u8"Ambiente João 日本");
  InitializeEnvironment(PathFromUtf8(package) / "app/workspace", root);
  auto targets = ReadCatalog(ProjectPaths{root});
  auto source_targets = ReadCatalog(ProjectPaths{PathFromUtf8(original)});
  REQUIRE(targets.size() >= 3);
  for (const auto& t : targets) {
    if (t.id != "castle_crashers" && t.id != "dash_17559") continue;
    auto it = std::find_if(source_targets.begin(), source_targets.end(), [&](const auto& s) { return s.id == t.id; });
    REQUIRE(it != source_targets.end());
    INFO(t.id);
    CHECK(Import(ProjectPaths{root}, t, it->game_root) == t.inputs.size());
    const auto state = Inspect(ProjectPaths{root}, t);
    CHECK(state.inputs_ok);
    CHECK_FALSE(state.prepared);
    CHECK(Import(ProjectPaths{root}, t, it->game_root) == 0);
    CHECK(Inspect(ProjectPaths{PathFromUtf8(original)}, *it).inputs_ok);
    if (t.id == "dash_17559") {
      // The menu needs its language typeface as well as SegoeXbox-Light.
      // A missing typeface must prevent a false "content complete" result.
      REQUIRE(fs::remove(t.game_root / "XenonJKLatin.xtt"));
      CHECK_FALSE(Inspect(ProjectPaths{root}, t).inputs_ok);
      CHECK(Import(ProjectPaths{root}, t, it->game_root) == 1);
      CHECK(Inspect(ProjectPaths{root}, t).inputs_ok);
    }
  }
  auto dash = std::find_if(targets.begin(), targets.end(), [](const auto& t) { return t.id == "dash_17559"; });
  REQUIRE(dash != targets.end());
  // Imported games without a native build must not block the dashboard or be
  // advertised as launchable content in its original library.
  REQUIRE(InspectLibrary(*dash, targets).empty());
  PrepareLibrary(ProjectPaths{root}, *dash, targets);
  CHECK(InspectLibrary(*dash, targets).empty());
  CHECK_NOTHROW(PrepareLibrary(ProjectPaths{root}, *dash, targets));
}

TEST_CASE("local isolated prepared environment installs original library dependencies", "[.local-prepared]") {
  const char* root = std::getenv("X360N_TEST_PREPARED_ROOT"); REQUIRE(root);
  ProjectPaths paths{PathFromUtf8(root)};
  const auto targets = ReadCatalog(paths);
  const auto dash = std::find_if(targets.begin(), targets.end(), [](const auto& t) { return t.id == "dash_17559"; });
  REQUIRE(dash != targets.end()); REQUIRE(Inspect(paths, *dash).prepared);
  for (const auto& id : dash->dependencies) {
    auto game = std::find_if(targets.begin(), targets.end(), [&](const auto& t) { return t.id == id; });
    REQUIRE(game != targets.end()); REQUIRE(Inspect(paths, *game).prepared);
  }
  PrepareLibrary(paths, *dash, targets);
  CHECK(InspectLibrary(*dash, targets).empty());
}
