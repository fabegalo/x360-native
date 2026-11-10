#include <catch2/catch_test_macros.hpp>
#include "preparation_fixture.h"
#include "child_process.h"
#include "workspace_lease.h"
#include "scratch.h"
#include "digest_batch.h"
#include "aot_pipeline.h"
#include "host_platform.h"
#include "sha256.h"
#include "toml_file.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
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
  // The supervisor runs it before the title; the synthetic receipt binds DLLs only.
  SECTION("unrecorded Vulkan preflight") { std::ofstream(f.exe.parent_path() / "x360n_gpu_preflight.exe") << "preflight"; }
  CHECK(f.Check());
}
// Session transitions re-hash what runs, not the build closure (main.cpp).
TEST_CASE("artifact scope re-hashes native artifacts and the manifest, not build inputs", "[preparation]") {
  Fixture f;
  auto artifacts = [&] { return CheckPreparation(f.paths, "sample", f.manifest, f.exe, ReceiptScope::Artifacts); };
  REQUIRE_FALSE(artifacts());
  std::ofstream(f.root / "compiler.exe") << "rebuilt compiler";
  CHECK(f.Check());
  CHECK_FALSE(artifacts());
  CHECK(CheckPreparedLaunch(f.paths, f.exe));
  CHECK_FALSE(CheckPreparedLaunch(f.paths, f.exe, ReceiptScope::Artifacts));
  f.Sign();
  for (const auto& file : {f.exe, f.exe.parent_path() / "runtime.dll", f.manifest}) {
    INFO(PathUtf8(file));
    std::ifstream in(file, std::ios::binary); std::string original{std::istreambuf_iterator<char>(in), {}}; in.close();
    std::ofstream(file, std::ios::binary) << original << " changed";
    CHECK(artifacts());
    std::ofstream(file, std::ios::binary) << original;
    REQUIRE_FALSE(artifacts());
  }
  std::ofstream(f.exe.parent_path() / "extra.dll") << "unrecorded";
  CHECK(artifacts());
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
TEST_CASE("receipt inventory snapshots preserve suffix and hierarchical glob gates", "[preparation]") {
  Fixture f;
  const auto directory = f.root / "templates";
  fs::create_directories(directory / "nested/deep");
  std::ofstream(directory / "root.H") << "header";
  std::ofstream(directory / "nested/one.c") << "one";
  std::ofstream(directory / "nested/deep/two.C") << "two";
  std::ofstream(directory / "nested/deep/other.txt") << "ignored";
  fs::create_directory(directory / "folder.h");
  const auto file = PreparationReceipt(f.exe);
  nlohmann::json receipt;
  { std::ifstream input(file); input >> receipt; }
  receipt["globs"] = nlohmann::json::array({
      {{"directory", PathUtf8(directory)}, {"pattern", "*.h"}, {"recursive", false}, {"directories", false},
       {"files", {PathUtf8(directory / "root.H")}}},
      {{"directory", PathUtf8(directory)}, {"pattern", "*.h"}, {"recursive", false}, {"directories", true},
       {"files", {PathUtf8(directory / "root.H"), PathUtf8(directory / "folder.h")}}},
      {{"directory", PathUtf8(directory)}, {"pattern", "*/*.c"}, {"recursive", false}, {"directories", false},
       {"files", {PathUtf8(directory / "nested/one.c")}}},
      {{"directory", PathUtf8(directory)}, {"pattern", "*.c"}, {"recursive", true}, {"directories", false},
       {"files", {PathUtf8(directory / "nested/one.c"), PathUtf8(directory / "nested/deep/two.C")}}}
  });
  std::ofstream(file) << receipt;
  {
    DigestBatch batch;
    REQUIRE_FALSE(f.Check());
    REQUIRE_FALSE(f.Check());
    SECTION("new matching source is never approved by the earlier snapshot") {
      std::ofstream(directory / "nested/deep/new.c") << "new";
    }
    SECTION("new unrecorded DLL is never approved by prior hashes") {
      std::ofstream(f.exe.parent_path() / "extra.DLL") << "new";
    }
    SECTION("a later incomplete marker revokes readiness") {
      fs::create_directories(f.paths.local() / "preparation");
      std::ofstream(f.paths.local() / "preparation/sample.incomplete") << "interrupted";
    }
    CHECK(f.Check());
  }
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
TEST_CASE("preparation requires every declared AOT dependency in its receipt", "[preparation]") {
  Fixture f;
  const auto module = f.root / "HostLoader.dll";
  std::ofstream(module) << "original guest module";
  std::ofstream(f.manifest, std::ios::app) << "[[modules]]\nfile_path='../../HostLoader.dll'\nguest_path='HostLoader.dll'\n";
  f.Sign(); CHECK(f.Check());
  f.inputs.push_back(module); f.Sign(); CHECK_FALSE(f.Check());
  const auto time = fs::last_write_time(module);
  std::ofstream(module) << "changed guest module"; fs::last_write_time(module, time);
  CHECK(f.Check());
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
TEST_CASE("listing hints follow receipt file times and never replace the byte check", "[preparation]") {
  Fixture f;
  { ReceiptHints hints{f.paths}; CHECK(hints.Check("sample",f.manifest,f.exe)==ReceiptHint::Current);
    CHECK(hints.Check("other",f.manifest,f.exe)==ReceiptHint::Stale); }
  SECTION("input changed after the receipt") {
    fs::last_write_time(f.inputs[0],fs::last_write_time(PreparationReceipt(f.exe))+std::chrono::hours(1));
  }
  SECTION("input missing") { fs::remove(f.inputs[1]); }
  SECTION("artifact missing") { fs::remove(f.exe.parent_path()/"runtime.dll"); }
  SECTION("other SDK revision") { std::ofstream(f.paths.sdk()/".git/HEAD") << std::string(40,'b') << '\n'; }
  SECTION("no receipt") { fs::remove(PreparationReceipt(f.exe)); }
  ReceiptHints hints{f.paths};
  CHECK(hints.Check("sample",f.manifest,f.exe)!=ReceiptHint::Current);
}
#ifdef _WIN32
// A deep installation prepares through its junction (%LOCALAPPDATA%\x360n\r),
// so the receipt names the manifest by that spelling and the listing by the
// installation's own; spelled as written, every such title read as stale.
TEST_CASE("listing hints accept a receipt written through the installation junction", "[preparation]") {
  Fixture f;
  const auto alias = f.root.parent_path() / "alias";
  wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
  ProcessSpec spec{fs::path(system) / "cmd.exe", {"/c", "mklink", "/J", PathUtf8(alias), PathUtf8(f.root)},
      f.root.parent_path(), f.root.parent_path() / "out", f.root.parent_path() / "err", std::chrono::seconds(3)};
  spec.no_console = true; REQUIRE(RunProcess(spec).exit_code == 0);
  WriteTestReceipt(f.paths, "sample", alias / f.manifest.lexically_relative(f.root), f.exe, f.inputs);
  {
    ReceiptHints hints{f.paths};
    CHECK(hints.Check("sample", f.manifest, f.exe) == ReceiptHint::Current);
  }
  CHECK_FALSE(f.Check());
  std::error_code ec; fs::remove(alias, ec);
}
#endif
TEST_CASE("a same-time replacement keeps the hint but fails the receipt check", "[preparation]") {
  Fixture f;
  const auto time = fs::last_write_time(f.inputs[0]);
  std::ofstream(f.inputs[0]) << "exchanged with preserved time";
  fs::last_write_time(f.inputs[0],time);
  ReceiptHints hints{f.paths};
  CHECK(hints.Check("sample",f.manifest,f.exe)==ReceiptHint::Current);
  CHECK(f.Check());
}
// Motocross Madness builds from a codegen root (.local/inputs) wider than its
// content; its registered default.xex was looked up in the codegen root.
TEST_CASE("registered originals resolve against the title's content root", "[preparation]") {
  const auto root = UniqueTempPath("content-root");
  const auto manifest = root / "titles/sample/sample_manifest.toml";
  fs::create_directories(manifest.parent_path());
  std::ofstream(manifest) << "[project]\ngame_root='../../.local/inputs'\n"
                             "[x360n]\ngame_data_root='../../.local/inputs/games/sample/content'\n";
  CHECK(RegisteredContentRoot(manifest).lexically_normal() ==
        (root / ".local/inputs/games/sample/content").lexically_normal());
  std::ofstream(manifest) << "[project]\ngame_root='../../.local/inputs/games/sample'\n";
  CHECK(RegisteredContentRoot(manifest).lexically_normal() ==
        (root / ".local/inputs/games/sample").lexically_normal());
  std::error_code ec; fs::remove_all(root, ec);
}
// An installed package has only what imports placed: Motocross Madness named
// the dashboard's xam.xex by a folder that only a development checkout had.
TEST_CASE("every original a title compiles is placed by an import", "[preparation]") {
  const auto key = [](const fs::path& path) {
    auto s = PathUtf8(path.lexically_normal());
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  std::set<std::string> placed;
  std::vector<fs::path> manifests;
  for (const auto& dir : fs::directory_iterator(fs::path(X360N_SOURCE_ROOT) / "titles")) {
    const auto manifest = dir.path() / (PathUtf8(dir.path().filename()) + "_manifest.toml");
    const auto registration = dir.path() / "preparation.toml";
    // Without a registration the launcher cannot import or prepare it.
    if (!fs::is_regular_file(manifest) || !fs::is_regular_file(registration)) continue;
    manifests.push_back(manifest);
    const auto content = RegisteredContentRoot(manifest);
    const auto setup = ParseTomlFile(registration);
    if (const auto* files = setup["files"].as_array())
      for (const auto& file : *files) {
        REQUIRE(file.is_table());
        placed.insert(key(content / PathFromUtf8((*file.as_table())["path"].value_or(std::string{}))));
      }
    if (const auto inventory = setup["inventory"].value<std::string>()) {
      std::ifstream in(dir.path() / PathFromUtf8(*inventory));
      for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (const auto tab = line.find('\t'); !line.starts_with('#') && tab != std::string::npos)
          placed.insert(key(content / PathFromUtf8(line.substr(tab + 1))));
      }
    }
  }
  REQUIRE_FALSE(manifests.empty());
  for (const auto& manifest : manifests) {
    const auto profile = ParseTomlFile(manifest);
    std::vector<std::string> originals{profile["entrypoint"]["file_path"].value_or(std::string{})};
    if (const auto* modules = profile["modules"].as_array())
      for (const auto& module : *modules) originals.push_back((*module.as_table())["file_path"].value_or(std::string{}));
    for (const auto& original : originals) {
      INFO(PathUtf8(manifest) << ": " << original);
      CHECK(placed.contains(key(manifest.parent_path() / PathFromUtf8(original))));
    }
  }
}
// A title's preset names the shared tools from its own folder; spelled as
// written, each title gave the shared SDK its own identity and rebuilt it.
TEST_CASE("title presets name a shared tool alike from every title folder", "[preparation][shared-sdk]") {
  const auto root = fs::absolute(UniqueTempPath("preset")).lexically_normal();
  const std::string python = "${sourceDir}/../../.local/toolchains/python/python.exe";
  const auto a = ExpandPresetValue(python, root / "titles/a");
  CHECK(a == ExpandPresetValue(python, root / "titles/b"));
  CHECK(PathFromUtf8(a) == (root / ".local/toolchains/python/python.exe").lexically_normal());
  CHECK(PathFromUtf8(ExpandPresetValue("${sourceParentDir}/x", root / "titles")) == root / "x");
  // Not a single path: expanded, otherwise kept as written.
  CHECK(ExpandPresetValue("ON", root) == "ON");
  CHECK(ExpandPresetValue("-DX=${sourceDir}/..", root) == "-DX=" + PathUtf8(root) + "/..");
  CHECK(ExpandPresetValue("${sourceDir}/a/..;b", root) == PathUtf8(root) + "/a/..;b");
  CHECK(ExpandPresetValue("${sourceDir}/${env:X}", root).find("${env:X}") != std::string::npos);
}
TEST_CASE("retiring shared SDK generations keeps those a preparation can still use", "[preparation][shared-sdk]") {
  const auto root = UniqueTempPath("shared-sdk-retire");
  const auto cache = root / "cache", owner = root / "owner", other = root / "other";
  const auto generation = [&](const fs::path& folder, const std::string& name, bool lock = true) {
    const auto dir = folder / name;
    fs::create_directories(dir);
    if (lock) std::ofstream(dir / ".x360n-build.lock");
    return dir;
  };
  // A receipt over one input, and the pointer that names it.
  const auto publish = [&](const fs::path& dir, const std::string& pointer) {
    const auto input = dir / "input.txt";
    std::ofstream(input) << "sdk input";
    const auto receipt = dir / "sdk.preparation.json";
    std::ofstream(receipt) << nlohmann::json{{"inputs", nlohmann::json::array({
        {{"path", PathUtf8(fs::absolute(input))}, {"sha256", DigestFile(input)->sha256}}})}}.dump();
    fs::create_directories(cache);
    std::ofstream(cache / (pointer + ".json")) << nlohmann::json{{"receipt", PathUtf8(fs::absolute(receipt))}}.dump();
    return input;
  };
  const auto current = generation(owner, "current");
  publish(current, "current");
  const auto verified = generation(owner, "verified");
  publish(verified, "verified");
  const auto changed = generation(owner, "changed");
  std::ofstream(publish(changed, "changed")) << "another SDK revision";
  const auto orphan = generation(owner, "orphan");
  const auto busy = generation(owner, "busy");
  const auto unrelated = generation(owner, "unrelated", false);
  const auto foreign = generation(other, "foreign");
  std::ofstream(publish(foreign, "foreign")) << "another owner's change";
  std::vector<std::string> retired;
  {
    FileLease in_use(busy / ".x360n-build.lock", true);
    retired = RetireSharedSdkGenerations(cache, current);
  }
  std::sort(retired.begin(), retired.end());
  CHECK(retired == std::vector<std::string>{"changed", "orphan"});
  CHECK_FALSE(fs::exists(changed));
  CHECK_FALSE(fs::exists(cache / "changed.json"));
  CHECK_FALSE(fs::exists(orphan));
  for (const auto& kept : {current, verified, busy, unrelated, foreign}) CHECK(fs::exists(kept));
  for (const auto& kept : {"current.json", "verified.json", "foreign.json"}) CHECK(fs::exists(cache / kept));
  // Released, the idle generation goes on the next publication.
  CHECK(RetireSharedSdkGenerations(cache, current) == std::vector<std::string>{"busy"});
  std::error_code ec;
  fs::remove_all(root, ec);
}
