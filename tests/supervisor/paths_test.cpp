#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "paths.h"
#include "scratch.h"

namespace fs = std::filesystem;
using namespace x360n;

namespace {

// A throwaway project tree: <tmp>/proj/{deps.lock.json,.local/{build,inputs}}
struct TempProject {
  fs::path root;
  TempProject() {
    root = UniqueTempPath("paths_test") / "proj";
    fs::remove_all(root.parent_path());
    fs::create_directories(root / ".local" / "build" / "cfg");
    fs::create_directories(root / ".local" / "inputs" / "game");
    fs::create_directories(root / "external" / "rexglue-sdk" / "out");
    std::ofstream(root / "deps.lock.json") << "{}";
    std::ofstream(root / ".local" / "build" / "cfg" / "tool.exe") << "x";
    std::ofstream(root / ".local" / "build" / "cfg" / "script.bat") << "x";
    std::ofstream(root / ".local" / "inputs" / "game" / "evil.exe") << "x";
    std::ofstream(root.parent_path() / "outside.exe") << "x";
#ifndef _WIN32
    // Exercise the same trust boundaries with native executable permissions.
    for (const auto& file : {root / ".local/build/cfg/tool.exe",
                            root / ".local/inputs/game/evil.exe",
                            root.parent_path() / "outside.exe"})
      fs::permissions(file, fs::perms::owner_exec, fs::perm_options::add);
#endif
  }
  ~TempProject() {
    std::error_code ec;
    fs::remove_all(root.parent_path(), ec);
  }
};

}  // namespace

TEST_CASE("finds the project root from a nested path", "[paths]") {
  TempProject t;
  auto p = FindProjectRoot(t.root / ".local" / "build" / "cfg" / "tool.exe");
  REQUIRE(p);
  CHECK(fs::equivalent(p->root, t.root));
}

TEST_CASE("IsWithin resolves dot-dot and rejects prefix look-alikes", "[paths]") {
  TempProject t;
  fs::create_directories(t.root / ".local" / "build2");
  CHECK(IsWithin(t.root / ".local" / "build" / "cfg", t.root / ".local" / "build"));
  CHECK_FALSE(IsWithin(t.root / ".local" / "build" / ".." / "inputs", t.root / ".local" / "build"));
  CHECK_FALSE(IsWithin(t.root / ".local" / "build2", t.root / ".local" / "build"));
  CHECK_FALSE(IsWithin(t.root / "does-not-exist", t.root));
}

TEST_CASE("only build outputs may be launched", "[paths]") {
  TempProject t;
  ProjectPaths p{t.root};
  CHECK_FALSE(CheckLaunchable(p, t.root / ".local" / "build" / "cfg" / "tool.exe"));
  CHECK(CheckLaunchable(p, t.root / ".local" / "build" / "cfg" / "script.bat"));
  CHECK(CheckLaunchable(p, t.root / ".local" / "inputs" / "game" / "evil.exe"));
  CHECK(CheckLaunchable(p, t.root.parent_path() / "outside.exe"));
  CHECK(CheckLaunchable(p, t.root / ".local" / "build" / "cfg" / "missing.exe"));
  // Escaping the build dir through ".." into inputs is still refused.
  CHECK(CheckLaunchable(p, t.root / ".local" / "build" / ".." / "inputs" / "game" / "evil.exe"));
}

TEST_CASE("package entry names cannot escape or alias devices", "[paths]") {
  // Names seen in the Castle Crashers package are accepted.
  CHECK(IsSafePackageEntryName("default.xex"));
  CHECK(IsSafePackageEntryName("SobelEdgePS.xpu"));
  CHECK(IsSafePackageEntryName("Achievement10.png"));
  // Traversal, separators, drive syntax.
  for (const char* bad : {"", ".", "..", "../x", "a/b", "a\\b", "C:", "x:y", "a*", "a?", "a|b", "\"q\""})
    CHECK_FALSE(IsSafePackageEntryName(bad));
  // Control characters, trailing dot/space (Windows strips them silently).
  CHECK_FALSE(IsSafePackageEntryName(std::string("a\x01", 2)));
  CHECK_FALSE(IsSafePackageEntryName("name."));
  CHECK_FALSE(IsSafePackageEntryName("name "));
  // Reserved device names, any case, with or without extension.
  for (const char* dev : {"CON", "nul", "Aux.txt", "COM1", "lpt9.dat", "PRN"})
    CHECK_FALSE(IsSafePackageEntryName(dev));
  CHECK(IsSafePackageEntryName("CONFIG.bin"));  // only the exact stem is reserved
  CHECK_FALSE(IsSafePackageEntryName(std::string(256, 'a')));
}

TEST_CASE("run labels are restricted to safe directory-name characters", "[paths]") {
  CHECK(IsSafeLabel("ppc-smoke_1.0"));
  CHECK_FALSE(IsSafeLabel(""));
  CHECK_FALSE(IsSafeLabel("../x"));
  CHECK_FALSE(IsSafeLabel("a/b"));
  CHECK_FALSE(IsSafeLabel("a\\b"));
  CHECK_FALSE(IsSafeLabel("C:x"));
  CHECK_FALSE(IsSafeLabel(std::string(65, 'a')));
}
