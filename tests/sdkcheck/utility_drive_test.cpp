#include <catch2/catch_test_macros.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "kernel/title_fixture.h"
#include <rex/filesystem/file_system.h>
#include <rex/filesystem/file.h>

namespace {
struct UtilityRoot {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("x360n-utility-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  UtilityRoot() { std::filesystem::create_directories(path / "game"); }
  ~UtilityRoot() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
struct UtilitySession {
  static rex::ppc::PPCImage Image(bool analysis) {
    return analysis ? rex::ppc::PPCImage{} : rex::ppc::PPCImage{0x82001000, 4, 0x82000000, 0x10000, nullptr};
  }
  UtilitySession(const std::filesystem::path& game, const std::filesystem::path& user,
                 const std::filesystem::path& cache = {}, bool analysis = false)
      : app("utility_contract", user.parent_path() / "utility-host"), runtime(Image(analysis)) {
    rex::test::ReleaseTestMemory();
    const std::vector<std::string> args{"--app.config_file=", "--app.user_root=" + user.string(),
                                        "--app.cache_root=" + cache.string()};
    REQUIRE(app.Init(rex::CLIArgList::Parse(args)));
    REQUIRE(runtime.GetTitlePaths().game_root.Set(game));
    REQUIRE(runtime.GetTitlePaths().entry.Set(""));
    REQUIRE(runtime.GetTitlePaths().imports.Set(""));
  }
  ~UtilitySession() {
    runtime.Unload();
    CHECK(runtime.GetTitlePaths().game_root.Reset());
    CHECK(runtime.GetTitlePaths().entry.Reset());
    CHECK(runtime.GetTitlePaths().imports.Reset());
    app.Shutdown();
  }
  rex::test::KernelHostApp app;
  rex::kernel::TitleRuntime runtime;
};
void CheckStream(rex::kernel::TitleRuntime& runtime, const char* name, bool create) {
  auto opened = runtime.GetFileSystem().Open(name, {
      .disposition=create ? rex::filesystem::Disposition::kOpenIf : rex::filesystem::Disposition::kOpen,
      .access=rex::filesystem::Access::kReadAccess | rex::filesystem::Access::kWriteAccess});
  REQUIRE(opened);
  auto& file = opened->file;
  const std::array<uint8_t, 4> expected{1, 2, 3, 4};
  if (create) {
    CHECK(opened->action == rex::filesystem::OpenAction::kCreated);
    auto bytes = file->Write(expected, 0); REQUIRE(bytes); CHECK(*bytes == expected.size());
    REQUIRE(file->Flush());
  } else {
    CHECK(opened->action == rex::filesystem::OpenAction::kOpened);
    std::array<uint8_t, 4> actual{};
    auto bytes = file->Read(actual, 0); REQUIRE(bytes); CHECK(*bytes == expected.size());
    CHECK(actual == expected);
  }
}
}

TEST_CASE("utility drive persists replay streams and isolates title roots", "[sdk][utility-drive]") {
  UtilityRoot root;
  for (const bool create : {true, false}) {
    UtilitySession session(root.path / "game", root.path / "title-a");
    REQUIRE(session.runtime.Load());
    CheckStream(session.runtime, "cache:\\replay_stream", create);
    CheckStream(session.runtime, "cache:\\ghost_stream_0", create);
  }
  CHECK(std::filesystem::file_size(root.path / "title-a/cache/utility/replay_stream") == 4);
  CHECK(std::filesystem::is_empty(root.path / "game"));
  UtilitySession other(root.path / "game", root.path / "title-b");
  REQUIRE(other.runtime.Load());
  CHECK_FALSE(other.runtime.GetFileSystem().Stat("cache:\\replay_stream"));
  CheckStream(other.runtime, "cache:\\replay_stream", true);
}

TEST_CASE("utility drive reuses the configured legacy cache root", "[sdk][utility-drive]") {
  UtilityRoot root;
  const auto cache = root.path / "legacy-cache";
  std::filesystem::create_directories(cache / "utility");
  { std::ofstream out(cache / "utility/replay_stream", std::ios::binary);
    const char bytes[]{1, 2, 3, 4}; out.write(bytes, sizeof(bytes)); }
  UtilitySession session(root.path / "game", root.path / "user", cache);
  REQUIRE(session.runtime.Load());
  CheckStream(session.runtime, "cache:\\replay_stream", false);
  CHECK_FALSE(std::filesystem::exists(root.path / "user/cache"));
}

TEST_CASE("utility drive setup reports storage errors and leaves analysis read only", "[sdk][utility-drive]") {
  UtilityRoot root;
  const auto cache = root.path / "blocked";
  { std::ofstream file(cache); file << "preserve"; }
  {
    UtilitySession session(root.path / "game", root.path / "user", cache);
    CHECK_FALSE(session.runtime.Load());
  }
  UtilitySession analysis(root.path / "game", root.path / "game", {}, true);
  REQUIRE(analysis.runtime.Load());
  CHECK_FALSE(analysis.runtime.GetFileSystem().Stat("cache:\\"));
  CHECK(std::filesystem::is_empty(root.path / "game"));
  CHECK(std::filesystem::file_size(cache) == 8);
}
