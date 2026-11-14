#include <catch2/catch_test_macros.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/file.h>

using namespace rex;
namespace {
struct UtilityRoot {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("x360n-utility-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  UtilityRoot() { std::filesystem::create_directories(path / "game"); }
  ~UtilityRoot() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
void CheckStream(Runtime& runtime, const char* name, bool create) {
  filesystem::File* file = nullptr;
  filesystem::FileAction action{};
  REQUIRE(runtime.file_system()->OpenFile(nullptr, name,
      create ? filesystem::FileDisposition::kOpenIf : filesystem::FileDisposition::kOpen,
      filesystem::FileAccess::kGenericRead | filesystem::FileAccess::kGenericWrite,
      false, true, &file, &action) == X_STATUS_SUCCESS);
  const auto close = [](filesystem::File* value) { value->Destroy(); };
  std::unique_ptr<filesystem::File, decltype(close)> owned(file, close);
  const std::array<uint8_t, 4> expected{1, 2, 3, 4};
  size_t bytes = 0;
  if (create) {
    CHECK(action == filesystem::FileAction::kCreated);
    REQUIRE(file->WriteSync(expected, 0, &bytes) == X_STATUS_SUCCESS);
    CHECK(bytes == expected.size());
    REQUIRE(file->Flush() == X_STATUS_SUCCESS);
  } else {
    CHECK(action == filesystem::FileAction::kOpened);
    std::array<uint8_t, 4> actual{};
    REQUIRE(file->ReadSync(actual, 0, &bytes) == X_STATUS_SUCCESS);
    CHECK(bytes == expected.size());
    CHECK(actual == expected);
  }
}
}

TEST_CASE("utility drive persists replay streams and isolates title roots", "[sdk][utility-drive]") {
  UtilityRoot root;
  for (const bool create : {true, false}) {
    Runtime runtime(root.path / "game", root.path / "title-a");
    REQUIRE(runtime.Setup(RuntimeConfig{}) == X_STATUS_SUCCESS);
    CheckStream(runtime, "cache:\\replay_stream", create);
    CheckStream(runtime, "cache:\\ghost_stream_0", create);
  }
  CHECK(std::filesystem::file_size(root.path / "title-a/cache/utility/replay_stream") == 4);
  CHECK(std::filesystem::is_empty(root.path / "game"));
  Runtime other(root.path / "game", root.path / "title-b");
  REQUIRE(other.Setup(RuntimeConfig{}) == X_STATUS_SUCCESS);
  CHECK(other.file_system()->ResolvePath("cache:\\replay_stream") == nullptr);
  CheckStream(other, "cache:\\replay_stream", true);
}

TEST_CASE("utility drive reuses the configured legacy cache root", "[sdk][utility-drive]") {
  UtilityRoot root;
  const auto cache = root.path / "legacy-cache";
  std::filesystem::create_directories(cache / "utility");
  { std::ofstream out(cache / "utility/replay_stream", std::ios::binary);
    const char bytes[]{1, 2, 3, 4}; out.write(bytes, sizeof(bytes)); }
  Runtime runtime(root.path / "game", root.path / "user", {}, cache);
  REQUIRE(runtime.Setup(RuntimeConfig{}) == X_STATUS_SUCCESS);
  CheckStream(runtime, "cache:\\replay_stream", false);
  CHECK_FALSE(std::filesystem::exists(root.path / "user/cache"));
}

TEST_CASE("utility drive setup reports storage errors and leaves analysis read only", "[sdk][utility-drive]") {
  UtilityRoot root;
  const auto cache = root.path / "blocked";
  { std::ofstream file(cache); file << "preserve"; }
  {
    Runtime runtime(root.path / "game", root.path / "user", {}, cache);
    CHECK(runtime.Setup(RuntimeConfig{}) == X_STATUS_UNSUCCESSFUL);
  }
  Runtime analysis(root.path / "game");
  RuntimeConfig config{}; config.tool_mode = true;
  REQUIRE(analysis.Setup(std::move(config)) == X_STATUS_SUCCESS);
  CHECK(analysis.file_system()->ResolvePath("cache:\\") == nullptr);
  CHECK(std::filesystem::is_empty(root.path / "game"));
  CHECK(std::filesystem::file_size(cache) == 8);
}
