#include <catch2/catch_test_macros.hpp>
#include "paths.h"
#include "scratch.h"
#include <fstream>

namespace fs = std::filesystem;
using namespace x360n;
namespace {
struct Fixture {
  fs::path root = UniqueTempPath("native-paths") / fs::path(u8"João 日本 🎮");
  ProjectPaths paths{root};
  Fixture() { fs::create_directories(paths.build()); fs::create_directories(paths.inputs()); }
  ~Fixture() { std::error_code ec; fs::remove_all(root.parent_path(), ec); }
  fs::path Executable(const fs::path& parent) {
#ifdef _WIN32
    const auto file = parent / "fixture.exe";
#else
    const auto file = parent / "fixture";
#endif
    fs::copy_file(X360N_TEST_CHILD, file);
    return file;
  }
};
}

TEST_CASE("native launch paths preserve Unicode and refuse content or executables outside the build", "[platform-paths]") {
  Fixture f;
  const auto executable = f.Executable(f.paths.build());
  CHECK_FALSE(CheckLaunchable(f.paths, executable));
  const auto content = f.Executable(f.paths.inputs());
  const auto refused = CheckLaunchable(f.paths, content);
  REQUIRE(refused);
  CHECK(refused->find("untrusted content") != std::string::npos);
  const auto outside = f.Executable(f.root);
  CHECK(CheckLaunchable(f.paths, outside));
  CHECK(IsWithin(executable, f.paths.build()));
  CHECK_FALSE(IsWithin(outside, f.paths.build()));
  CHECK(IsWithin(executable, executable.root_path()));
  CHECK(PathFromUtf8(PathUtf8(executable)) == executable);
#ifndef _WIN32
  fs::permissions(executable, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::remove);
  CHECK(CheckLaunchable(f.paths, executable));
  // Links never bypass the trust boundary, even when the link itself is in
  // the build directory or its name has an executable suffix.
  fs::create_symlink(content, f.paths.build() / "redirect");
  CHECK(CheckLaunchable(f.paths, f.paths.build() / "redirect"));
#endif
}

TEST_CASE("native executable identity is absolute and independent of current directory", "[platform-paths]") {
  Fixture f;
  const auto executable = CurrentExecutablePath();
  REQUIRE(executable.is_absolute());
  REQUIRE(fs::is_regular_file(executable));
  CHECK(CurrentProcessId() != 0);
  struct RestoreDirectory {
    fs::path path = fs::current_path();
    ~RestoreDirectory() { std::error_code ec; fs::current_path(path, ec); }
  } restore;
  fs::current_path(f.root);
  CHECK(CurrentExecutablePath() == executable);
}

#ifndef _WIN32
TEST_CASE("native containment treats POSIX backslashes and filename case literally", "[platform-paths]") {
  Fixture f;
  const auto base = f.root / "base";
  const auto sibling = f.root / "base\\escape";
  fs::create_directories(base);
  fs::create_directories(sibling);
  CHECK_FALSE(IsWithin(sibling, base));
  CHECK(IsWithin(base, f.root));
  fs::create_directory(f.root / "Case");
  fs::create_directory(f.root / "case");
  if (!fs::equivalent(f.root / "Case", f.root / "case"))
    CHECK_FALSE(IsWithin(f.root / "Case", f.root / "case"));
}
#endif
