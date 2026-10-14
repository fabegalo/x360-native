#include <catch2/catch_test_macros.hpp>
#include "preparation_fixture.h"
#include "child_process.h"
#include "workspace_lease.h"
#include "scratch.h"
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace x360n;
namespace {
struct Fixture {
  fs::path root = UniqueTempPath("preparation") / fs::path(u8"João 日本");
  ProjectPaths paths{root};
  fs::path manifest = root / "titles/sample/sample_manifest.toml";
  fs::path exe = paths.build() / "titles/sample/sample.exe";
  std::vector<fs::path> inputs;
  Fixture() {
    fs::create_directories(manifest.parent_path()); fs::create_directories(exe.parent_path());
    for (auto name : {"default.xex", "analysis.toml", "patch.diff", "template.inja", "compiler.exe", "codegen.exe", "options.txt"}) {
      inputs.push_back(root / name); std::ofstream(inputs.back()) << "original fixture";
    }
    std::ofstream(manifest) << "[project]\nname='sample'\n[entrypoint]\nfile_path='../../default.xex'\n";
    std::ofstream(exe) << "native fixture";
    std::ofstream(exe.parent_path() / "runtime.dll") << "original runtime";
    Sign();
  }
  void Sign() { WriteTestReceipt(paths, "sample", manifest, exe, inputs); }
  auto Check() { return CheckPreparation(paths, "sample", manifest, exe); }
  ~Fixture() { std::error_code ec; fs::remove_all(root.parent_path(), ec); }
};
}
TEST_CASE("preparation binds content tools configuration and native artifacts by bytes", "[preparation]") {
  Fixture f; REQUIRE_FALSE(f.Check());
  auto files = f.inputs; files.push_back(f.manifest); files.push_back(f.exe); files.push_back(f.exe.parent_path() / "runtime.dll");
  for (const auto& file : files) {
    INFO(PathUtf8(file));
    std::ifstream in(file, std::ios::binary); std::string original{std::istreambuf_iterator<char>(in), {}}; in.close();
    const auto time = fs::last_write_time(file);
    auto changed = original; changed[0] ^= 1;
    std::ofstream(file, std::ios::binary) << changed; fs::last_write_time(file, time);
    auto error = f.Check(); REQUIRE(error); CHECK(error->find("prepare novamente") != std::string::npos);
    std::ofstream(file, std::ios::binary) << original; fs::last_write_time(file, time);
    REQUIRE_FALSE(f.Check());
  }
  std::ofstream(f.root / "unrelated-title-analysis.toml") << "unrelated";
  CHECK_FALSE(f.Check());
  std::ofstream(f.paths.sdk() / ".git/HEAD") << std::string(40, 'b'); CHECK(f.Check());
}
TEST_CASE("missing partial wrong-title and incomplete receipts never qualify a native exe", "[preparation]") {
  Fixture f;
  SECTION("missing") { fs::remove(PreparationReceipt(f.exe)); }
  SECTION("partial") { std::ofstream(PreparationReceipt(f.exe)) << "{\"schema\":1,"; }
  SECTION("wrong title") { WriteTestReceipt(f.paths, "another", f.manifest, f.exe, f.inputs); }
  SECTION("incomplete") { fs::create_directories(f.paths.local() / "preparation"); std::ofstream(f.paths.local() / "preparation/sample.incomplete") << "incomplete"; }
  SECTION("unrecorded DLL") { std::ofstream(f.exe.parent_path() / "extra.dll") << "different"; }
  SECTION("deleted DLL") { fs::remove(f.exe.parent_path() / "runtime.dll"); }
  CHECK(f.Check());
}
TEST_CASE("analysis invalidation follows the affected title dependency closure", "[preparation]") {
  Fixture f;
  auto other = f.root / "titles/other/other_manifest.toml";
  auto exe = f.paths.build() / "titles/other/other.exe";
  fs::create_directories(other.parent_path()); fs::create_directories(exe.parent_path());
  std::ofstream(other) << "[entrypoint]\nfile_path='../../other.xex'\n";
  std::ofstream(f.root / "other.xex") << "other content";
  std::ofstream(exe) << "other native build";
  WriteTestReceipt(f.paths, "other", other, exe, {f.root / "other.xex", f.inputs[2]});
  std::ofstream(f.inputs[1]) << "changed sample analysis";
  CHECK(f.Check());
  CHECK_FALSE(CheckPreparation(f.paths, "other", other, exe));
  std::ofstream(f.manifest) << "[invalid TOML";
  CHECK_FALSE(CheckPreparedLaunch(f.paths, exe));
  std::ofstream(f.inputs[2]) << "changed shared patch";
  CHECK(CheckPreparation(f.paths, "other", other, exe));
}
TEST_CASE("new templates invalidate the configured source inventory", "[preparation]") {
  Fixture f;
  auto directory = f.root / "templates"; fs::create_directories(directory);
  auto receipt = PreparationReceipt(f.exe);
  std::ifstream in(receipt); std::string json{std::istreambuf_iterator<char>(in), {}}; in.close();
  const auto pos = json.find('[', json.find("\"globs\"")); REQUIRE(pos != std::string::npos);
  json.replace(pos, json.find(']', pos) - pos + 1,
      "[{\"directory\":" + JsonEscape(PathUtf8(directory)) +
      ",\"pattern\":\"*.inja\",\"recursive\":true,\"directories\":false,\"files\":[]}]");
  std::ofstream(receipt) << json;
  REQUIRE_FALSE(f.Check());
  std::ofstream(directory / "notes.txt") << "not a template"; CHECK_FALSE(f.Check());
  fs::create_directories(directory / "nested");
  std::ofstream(directory / "nested/new.inja") << "new template"; CHECK(f.Check());
}
TEST_CASE("explicit alternate build remains bound to the registered title", "[preparation]") {
  Fixture f;
  auto exe = f.paths.build() / "alternate/sample.exe"; fs::create_directories(exe.parent_path());
  fs::copy_file(f.exe, exe);
  WriteTestReceipt(f.paths, "sample", f.manifest, exe, f.inputs);
  CHECK_FALSE(CheckPreparedLaunch(f.paths, exe));
  std::ofstream(exe) << "wrong native version";
  CHECK(CheckPreparedLaunch(f.paths, exe));
}
TEST_CASE("compact AOT receipts require the matching directory owner", "[preparation]") {
  Fixture f;
  const auto exe = f.paths.build() / "aot/short/sample.exe";
  fs::create_directories(exe.parent_path()); fs::copy_file(f.exe, exe);
  const auto owner = exe.parent_path() / ".x360n-aot-owner";
  std::ofstream(owner) << "sample\nfull-content-identity\n";
  WriteTestReceipt(f.paths, "sample", f.manifest, exe, f.inputs);
  CHECK(CheckPreparation(f.paths, "sample", f.manifest, exe));
  f.inputs.push_back(owner);
  WriteTestReceipt(f.paths, "sample", f.manifest, exe, f.inputs);
  CHECK_FALSE(CheckPreparation(f.paths, "sample", f.manifest, exe));
  std::ofstream(owner) << "another\nfull-content-identity\n";
  WriteTestReceipt(f.paths, "sample", f.manifest, exe, f.inputs);
  CHECK(CheckPreparation(f.paths, "sample", f.manifest, exe));
}
#ifdef _WIN32
TEST_CASE("inspection rejects the title producer lease and recovers after release", "[preparation]") {
  Fixture f;
  fs::create_directories(f.paths.local() / "preparation");
  const auto file = f.paths.local() / "preparation/sample.lock";
  auto owner = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  REQUIRE(owner != INVALID_HANDLE_VALUE);
  const auto error = f.Check();
  CloseHandle(owner);
  CHECK(error);
  CHECK_FALSE(f.Check());
}
#endif
TEST_CASE("supervisor blocks an exchanged executable or DLL before creating a child run", "[preparation][process]") {
  Fixture f;
  fs::copy_file(X360N_TEST_CHILD, f.exe, fs::copy_options::overwrite_existing); f.Sign();
  ProcessSpec spec{X360N_EXE, {"--root", PathUtf8(f.root), "run", PathUtf8(f.exe), "--", "exit", "0"},
      f.root, f.root / "out", f.root / "err", std::chrono::seconds(20)};
  SECTION("correct preparation runs and is reusable") {
    REQUIRE_FALSE(f.Check());
    auto r = RunProcess(spec); CHECK(r.launched); CHECK(r.exit_code == 0); CHECK(fs::exists(f.paths.runs()));
    REQUIRE_FALSE(f.Check());
  }
  SECTION("different executable") {
    fs::copy_file(X360N_EXE, f.exe, fs::copy_options::overwrite_existing);
    auto r = RunProcess(spec); CHECK(r.launched); CHECK(r.exit_code == 3); CHECK_FALSE(fs::exists(f.paths.runs()));
  }
  SECTION("different DLL") {
    fs::copy_file(X360N_TEST_CHILD, f.exe.parent_path() / "runtime.dll", fs::copy_options::overwrite_existing);
    auto r = RunProcess(spec); CHECK(r.launched); CHECK(r.exit_code == 3); CHECK_FALSE(fs::exists(f.paths.runs()));
  }
  SECTION("build owns the environment") {
    WorkspaceLease owner(f.paths);
    auto r = RunProcess(spec); CHECK(r.launched); CHECK(r.exit_code == 3); CHECK_FALSE(fs::exists(f.paths.runs()));
  }
  SECTION("incomplete staging never launches even with unchanged final artifacts") {
    fs::create_directories(f.paths.local() / "preparation");
    std::ofstream(f.paths.local() / "preparation/sample.incomplete") << "incomplete";
    std::ofstream(f.exe.parent_path() / "runtime.dll.stage-tmp") << "partial replacement";
    CHECK(f.Check());
    auto r = RunProcess(spec); CHECK(r.launched); CHECK(r.exit_code == 3); CHECK_FALSE(fs::exists(f.paths.runs()));
  }
}
