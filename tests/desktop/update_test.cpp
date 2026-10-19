// Self-update decisions and file replacement on synthetic packages; no network.
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "catalog.h"
#include "sha256.h"
#include "update.h"
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
  const auto found = SelectUpdate(json, "0.54.0");
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
  Write(rollback / "stale.txt", "older rollback");
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
    CHECK(Read(rollback / "x360-native.exe") == "launcher 1.0.0");
    CHECK(Read(rollback / "README-old.txt") == "README-old.txt 1.0.0");
    CHECK_FALSE(fs::exists(rollback / "stale.txt"));
    CHECK_NOTHROW(VerifyPackage(rollback));
  }
  fs::remove_all(root);
}
