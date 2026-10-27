// Self-update decisions and file replacement on synthetic packages; no network.
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <algorithm>
#include "catalog.h"
#include "sha256.h"
#include "update.h"
#include "child_process.h"
#include <nlohmann/json.hpp>
#include "../supervisor/scratch.h"

namespace fs = std::filesystem;
using namespace x360n::desktop;
using x360n::DigestFile;
namespace {
void Write(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << text;
}
std::string Read(const fs::path& file) {
  std::ifstream in(file, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}};
}
// Minimal package that VerifyPackage accepts: required files, payload and package indexes.
void MakePackage(const fs::path& dir, const std::string& version, const std::vector<std::string>& extra = {}) {
  Write(dir / "x360-native.exe", "launcher " + version);
  Write(dir / "app/x360n.exe", "supervisor " + version);
  Write(dir / "app/x360n-extract.exe", "extractor " + version);
  Write(dir / "app/workspace/deps.lock.json", "{\"version\":\"" + version + "\"}");
  Write(dir / "app/workspace/payload.sha256", DigestFile(dir / "app/workspace/deps.lock.json")->sha256 + "\tdeps.lock.json\n");
  Write(dir / "package.json", "{\"version\":\"" + version + "\"}");
  for (const auto& name : extra) Write(dir / name, name + " " + version);
  std::string index;
  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    const auto rel = entry.path().lexically_relative(dir);
    if (rel == "package.sha256" || *rel.begin() == "data") continue;
    const auto name = rel.generic_u8string();
    index += DigestFile(entry.path())->sha256 + "\t" + std::string(name.begin(), name.end()) + "\n";
  }
  Write(dir / "package.sha256", index);
}
}
TEST_CASE("versions follow Semantic Versioning precedence", "[update]") {
  const char* ordered[] = {"0.1.0-preview.22", "0.54.0-dev", "0.54.0", "0.54.1", "0.55.0", "1.0.0-alpha", "1.0.0-alpha.1",
                           "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "2.0.0", "10.0.0"};
  for (size_t i = 0; i + 1 < std::size(ordered); ++i) {
    INFO(ordered[i] << " < " << ordered[i + 1]);
    CHECK(CompareVersions(ordered[i], ordered[i + 1]) == -1);
    CHECK(CompareVersions(ordered[i + 1], ordered[i]) == 1);
  }
  CHECK(CompareVersions("1.2.3+build.5", "1.2.3") == 0);
  CHECK_FALSE(CompareVersions("01.0.0", "1.0.0"));
  CHECK_FALSE(CompareVersions("v1.0.0", "1.0.0"));
  CHECK_FALSE(CompareVersions("1.0", "1.0.0"));
}
TEST_CASE("the newest complete published release is selected with pinned URLs", "[update]") {
  const std::string assets = R"("assets":[{"name":"x360-native-VER-windows-x64.zip","browser_download_url":"https://evil.example/x.zip"},{"name":"x360-native-VER-windows-x64.zip.sha256"}])";
  auto release = [&](const std::string& tag, bool draft, bool complete) {
    auto list = assets; const auto version = tag.substr(tag[0] == 'v' ? 1 : 0);
    for (size_t at; (at = list.find("VER")) != std::string::npos;) list.replace(at, 3, version);
    if (!complete) list = R"("assets":[{"name":"x360-native-)" + version + R"(-windows-x64.zip"}])";
    return R"({"tag_name":")" + tag + R"(","draft":)" + (draft ? "true" : "false") + R"(,"prerelease":true,)" + list + "}";
  };
  const auto json = "[" + release("v0.60.0", true, true) + "," + release("v0.59.0", false, false) + "," + release("0.58.0", false, true) + "," +
                    release("v0.57.0", false, true) + "," + release("v0.56.1", false, true) + "," + release("v0.50.0", false, true) + "]";
  CHECK_FALSE(SelectUpdate(json, "0.54.0")); // Every fixture release is marked prerelease.
  const auto found = SelectUpdate(json, "0.54.0", UpdateChannel::Preview);
  REQUIRE(found);
  CHECK(found->version == "0.57.0");
  CHECK(found->package == "x360-native-0.57.0-windows-x64");
  CHECK(found->archive_url == "https://github.com/fabegalo/x360-native/releases/download/v0.57.0/x360-native-0.57.0-windows-x64.zip");
  CHECK(found->checksum_url == found->archive_url + ".sha256");
  CHECK(found->page_url == "https://github.com/fabegalo/x360-native/releases/tag/v0.57.0");
  CHECK_FALSE(SelectUpdate(json, "0.57.0"));
  CHECK_FALSE(SelectUpdate("[]", "0.54.0"));
  CHECK_THROWS(SelectUpdate("{\"message\":\"API rate limit exceeded\"}", "0.54.0"));
}
TEST_CASE("a downloaded archive must match its published checksum line", "[update]") {
  const auto dir = UniqueTempPath("update-archive");
  const auto archive = dir / "x360-native-1.0.0-windows-x64.zip", checksum = dir / "x360-native-1.0.0-windows-x64.zip.sha256";
  Write(archive, "zip bytes");
  const auto hash = DigestFile(archive)->sha256;
  Write(checksum, hash + "  x360-native-1.0.0-windows-x64.zip\n");
  CHECK_NOTHROW(VerifyArchive(archive, checksum));
  Write(checksum, hash + "  other.zip\n");
  CHECK_THROWS(VerifyArchive(archive, checksum));
  Write(checksum, std::string(64, '0') + "  x360-native-1.0.0-windows-x64.zip\n");
  CHECK_THROWS(VerifyArchive(archive, checksum));
  fs::remove_all(dir);
}
TEST_CASE("an update replaces application files, keeps data and restores on failure", "[update]") {
  const auto root = UniqueTempPath("update-apply");
  const auto package = root / "installed", staged = root / "staged" / "x360-native-2.0.0-windows-x64", rollback = package / "data/updates/previous";
  MakePackage(package, "1.0.0", {"README-old.txt", "licenses/a.txt"});
  Write(package / "data/preferences.txt", "pt\n.\n");
  Write(package / "data/environment/save.bin", "user save");
  MakePackage(staged, "2.0.0", {"licenses/a.txt", "licenses/b.txt"});

  SECTION("version mismatch changes nothing") {
    CHECK_THROWS(ApplyUpdate(package, staged, "3.0.0", rollback));
    CHECK(Read(package / "x360-native.exe") == "launcher 1.0.0");
    CHECK(fs::exists(staged / "x360-native.exe"));
  }
  SECTION("a locked file rolls every move back") {
    std::ifstream held(package / "x360-native.exe", std::ios::binary);  // no delete sharing: the rename fails
    CHECK_THROWS(ApplyUpdate(package, staged, "2.0.0", rollback));
    held.close();
    CHECK(Read(package / "x360-native.exe") == "launcher 1.0.0");
    CHECK(Read(package / "app/x360n.exe") == "supervisor 1.0.0");
    CHECK(Read(package / "README-old.txt") == "README-old.txt 1.0.0");
    CHECK(Read(staged / "app/x360n.exe") == "supervisor 2.0.0");
    CHECK(Read(package / "data/environment/save.bin") == "user save");
  }
  SECTION("success") {
    ApplyUpdate(package, staged, "2.0.0", rollback);
    CHECK_NOTHROW(VerifyPackage(package));
    CHECK(Read(package / "x360-native.exe") == "launcher 2.0.0");
    CHECK(Read(package / "licenses/b.txt") == "licenses/b.txt 2.0.0");
    CHECK_FALSE(fs::exists(package / "README-old.txt"));
    CHECK(Read(package / "data/preferences.txt") == "pt\n.\n");
    CHECK(Read(package / "data/environment/save.bin") == "user save");
    CHECK(Read(rollback / "backup/x360-native.exe") == "launcher 1.0.0");
    CHECK(Read(rollback / "backup/README-old.txt") == "README-old.txt 1.0.0");
    CHECK_NOTHROW(VerifyPackage(rollback / "backup"));
    CHECK(fs::exists(package / "recover-update.cmd"));
    CHECK(fs::exists(rollback / "recovery.exe"));
    CHECK_NOTHROW(ConfirmUpdates(package, "2.0.0"));
    CHECK(fs::exists(rollback / "confirmed"));
    CHECK_NOTHROW(RecoverUpdate(package, rollback));
    CHECK_NOTHROW(RecoverUpdate(package, rollback));
    CHECK(Read(package / "x360-native.exe") == "launcher 1.0.0");
  }
  fs::remove_all(root);
}

TEST_CASE("stable updates exclude prerelease tags and GitHub prerelease flags", "[update]") {
  using nlohmann::json;
  auto release = [](std::string version, bool preview) {
    const auto stem = "x360-native-" + version + "-windows-x64.zip";
    return json{{"tag_name", "v" + version}, {"draft", false}, {"prerelease", preview},
                {"assets", json::array({json{{"name", stem}}, json{{"name", stem + ".sha256"}}})}};
  };
  const auto releases = json::array({release("2.0.0", false), release("3.0.0-rc.1", false), release("4.0.0", true)}).dump();
  REQUIRE(SelectUpdate(releases, "1.0.0"));
  CHECK(SelectUpdate(releases, "1.0.0")->version == "2.0.0");
  CHECK(SelectUpdate(releases, "1.0.0", UpdateChannel::Preview)->version == "4.0.0");
}

namespace {
uint32_t RunProbe(const fs::path& root, const std::string& mode, int step) {
  x360n::ProcessSpec spec;
  spec.exe = X360N_UPDATE_PROBE;
  spec.args = {mode, Utf8(root / "installed"), Utf8(root / "staged"), Utf8(root / "installed/data/updates/txn-test"), std::to_string(step)};
  spec.working_dir = root; spec.timeout = std::chrono::seconds(15); spec.no_console = true;
  spec.stdout_file = root / "probe-out.txt"; spec.stderr_file = root / "probe-error.txt";
  auto result = x360n::RunProcess(spec);
  INFO(Read(spec.stderr_file));
  REQUIRE(result.launched); REQUIRE_FALSE(result.timed_out);
  return result.exit_code;
}
void Packages(const fs::path& root) {
  MakePackage(root / "installed", "1.0.0", {"old.txt", "licenses/old.txt"});
  MakePackage(root / "staged", "2.0.0", {"new.txt", "licenses/new.txt"});
  Write(root / "installed/data/profiles/profile.bin", "profile");
  Write(root / "installed/data/saves/save.bin", "save");
  Write(root / "installed/data/library/imported.bin", "imported");
  Write(root / "installed/local-not-in-index.txt", "user file");
  // The console has one HDD; profile and per-user/title saves are content.
  // Include binary/truncated payloads: updater must preserve opaque bytes,
  // never interpret or repair a game's save format.
  for (const auto* relative : {"E000000000000001/FFFE07D1/00010000/profile",
       "E000000000000001/1234C800/00000001/progress/data",
       "E000000000000002/1234C800/00000001/progress/data",
       "E000000000000001/1234C801/00000001/progress/data"})
    Write(root / "installed/data/console/HDD/Content" / relative,
          std::string("\0\xffpartial\0", 10) + relative);
}
void Preserved(const fs::path& root) {
  CHECK(Read(root / "installed/data/profiles/profile.bin") == "profile");
  CHECK(Read(root / "installed/data/saves/save.bin") == "save");
  CHECK(Read(root / "installed/data/library/imported.bin") == "imported");
  CHECK(Read(root / "installed/local-not-in-index.txt") == "user file");
  for (const auto* relative : {"E000000000000001/FFFE07D1/00010000/profile",
       "E000000000000001/1234C800/00000001/progress/data",
       "E000000000000002/1234C800/00000001/progress/data",
       "E000000000000001/1234C801/00000001/progress/data"})
    CHECK(Read(root / "installed/data/console/HDD/Content" / relative) ==
          std::string("\0\xffpartial\0", 10) + relative);
}
}

TEST_CASE("process death at every update boundary is recoverable by a new process", "[update][update-crash]") {
  // 9 indexed files in each package, two boundaries per move, plus journal and installed.
  const auto baseline = UniqueTempPath("update-count"); Packages(baseline);
  REQUIRE(RunProbe(baseline, "apply", 0) == 0);
  const auto output = Read(baseline / "probe-out.txt");
  const int count = std::stoi(output.substr(output.find('=') + 1));
  REQUIRE(count > 20);
  fs::remove_all(baseline);
  for (int step = 1; step <= count; ++step) {
    CAPTURE(step, count);
    const auto root = UniqueTempPath("update-crash"); Packages(root);
    REQUIRE(RunProbe(root, "apply", step) == 91);
    REQUIRE(RunProbe(root, "recover", 0) == 0);
    CHECK(Read(root / "installed/x360-native.exe") == "launcher 1.0.0");
    CHECK_NOTHROW(VerifyPackage(root / "installed"));
    Preserved(root);
    REQUIRE(RunProbe(root, "recover", 0) == 0);
    fs::remove_all(root);
  }
}

TEST_CASE("rollback itself can be resumed after process death", "[update][update-crash]") {
  const auto baseline = UniqueTempPath("rollback-count"); Packages(baseline);
  REQUIRE(RunProbe(baseline, "apply", 0) == 0);
  REQUIRE(RunProbe(baseline, "recover", 0) == 0);
  auto out = Read(baseline / "probe-out.txt");
  const int count = std::stoi(out.substr(out.find('=') + 1));
  fs::remove_all(baseline);
  for (int step = 1; step <= count; ++step) {
    CAPTURE(step);
    const auto root = UniqueTempPath("rollback-crash"); Packages(root);
    REQUIRE(RunProbe(root, "apply", 0) == 0);
    REQUIRE(RunProbe(root, "recover", step) == 91);
    REQUIRE(RunProbe(root, "recover", 0) == 0);
    CHECK_NOTHROW(VerifyPackage(root / "installed")); Preserved(root);
    fs::remove_all(root);
  }
}

TEST_CASE("update rejects invalid paths inventories and collisions before moving application files", "[update]") {
  const auto root = UniqueTempPath("update-invalid"); Packages(root);
  const auto package = root / "installed", staged = root / "staged", txn = package / "data/updates/txn-test";
  SECTION("staging hash mismatch") { Write(staged / "x360-native.exe", "tampered"); }
  SECTION("unindexed staging file") { Write(staged / "unindexed.txt", "unknown"); }
  SECTION("case folded data") { Write(staged / "DATA/saves.bin", "forbidden"); }
  SECTION("inventory traversal") { std::ofstream(staged / "package.sha256", std::ios::app) << std::string(64, 'a') << "\t../escape\n"; }
  SECTION("corrupt installed inventory") { Write(package / "package.sha256", "invalid"); }
  SECTION("local collision") { Write(package / "new.txt", "local user file"); }
  SECTION("local recovery script collision") { Write(package / "recover-update.cmd", "user script"); }
  SECTION("previous transaction preserved") { Write(txn / "backup/old.txt", "previous backup"); }
  CHECK_THROWS(ApplyUpdate(package, staged, "2.0.0", txn));
  CHECK(Read(package / "x360-native.exe") == "launcher 1.0.0");
  Preserved(root);
  fs::remove_all(root);
}

TEST_CASE("consecutive update transactions preserve previous backups", "[update]") {
  const auto root = UniqueTempPath("update-consecutive"); Packages(root);
  const auto package = root / "installed", first = package / "data/updates/txn-first", second = package / "data/updates/txn-second";
  ApplyUpdate(package, root / "staged", "2.0.0", first);
  MakePackage(root / "third", "3.0.0");
  CHECK_THROWS(ApplyUpdate(package, root / "third", "3.0.0", second));
  CHECK_NOTHROW(ConfirmUpdates(package, "2.0.0"));
  ApplyUpdate(package, root / "third", "3.0.0", second);
  CHECK(Read(first / "backup/x360-native.exe") == "launcher 1.0.0");
  CHECK(Read(second / "backup/x360-native.exe") == "launcher 2.0.0");
  CHECK_NOTHROW(RecoverUpdate(package, second));
  CHECK(Read(package / "x360-native.exe") == "launcher 2.0.0");
  Preserved(root);
  fs::remove_all(root);
}

TEST_CASE("rollback failure remains explicit and recoverable after releasing the lock", "[update]") {
  const auto root = UniqueTempPath("update-rollback-locked"); Packages(root);
  const auto package = root / "installed", txn = package / "data/updates/txn-test";
  ApplyUpdate(package, root / "staged", "2.0.0", txn);
  std::ifstream held(package / "x360-native.exe", std::ios::binary);
  CHECK_THROWS(RecoverUpdate(package, txn));
  CHECK_FALSE(fs::exists(txn / "rolled-back"));
  held.close();
  CHECK_NOTHROW(RecoverUpdate(package, txn));
  CHECK_NOTHROW(VerifyPackage(package)); Preserved(root);
  fs::remove_all(root);
}

TEST_CASE("the saved supervisor recovers when the main executable is missing or cannot start", "[update][update-crash]") {
  const auto root = UniqueTempPath("update-independent-helper"); Packages(root);
  const auto package = root / "installed", txn = package / "data/updates/txn-test";
  fs::copy_file(X360N_RECOVERY_EXE, package / "app/x360n.exe", fs::copy_options::overwrite_existing);
  auto index = Read(package / "package.sha256");
  const auto line_end = index.find("\tapp/x360n.exe");
  REQUIRE(line_end != std::string::npos);
  index.replace(line_end - 64, 64, DigestFile(package / "app/x360n.exe")->sha256);
  Write(package / "package.sha256", index);
  SECTION("power loss after the launcher was renamed") {
    const auto files = int(std::count(index.begin(), index.end(), '\n')) + 1;
    REQUIRE(RunProbe(root, "apply", 1 + 2 * files) == 91);
    REQUIRE_FALSE(fs::exists(package / "x360-native.exe"));
  }
  SECTION("the replacement executable is invalid") {
    REQUIRE(RunProbe(root, "apply", 0) == 0);
    x360n::ProcessSpec broken;
    broken.exe = package / "x360-native.exe"; broken.working_dir = package;
    broken.timeout = std::chrono::seconds(5); broken.no_console = true;
    broken.stdout_file = root / "broken-out.txt"; broken.stderr_file = root / "broken-error.txt";
    CHECK_FALSE(x360n::RunProcess(broken).launched);
  }
  x360n::ProcessSpec recovery;
  recovery.exe = txn / "recovery.exe"; recovery.working_dir = txn;
  recovery.args = {"recover-update", "--package", Utf8(package), "--transaction", Utf8(txn)};
  recovery.timeout = std::chrono::seconds(15); recovery.no_console = true;
  recovery.stdout_file = root / "recovery-out.txt"; recovery.stderr_file = root / "recovery-error.txt";
  const auto result = x360n::RunProcess(recovery);
  INFO(Read(recovery.stderr_file));
  REQUIRE(result.launched); CHECK(result.exit_code == 0);
  CHECK_NOTHROW(VerifyPackage(package));
  CHECK(Read(package / "x360-native.exe") == "launcher 1.0.0");
  Preserved(root);
  fs::remove_all(root);
}
