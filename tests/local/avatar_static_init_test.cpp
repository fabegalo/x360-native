// The avatar service runs a fixed block of the 17559 xam.xex static initializer
// table (titles/common/avatar/xam17559_static_init.h). Checks that block and
// the image values that make it necessary against the user's own xam.xex;
// skipped when the file is absent.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../../titles/common/avatar/xam17559_static_init.h"
#include "../supervisor/scratch.h"
#include "child_process.h"

namespace fs = std::filesystem;
using namespace x360n;
using namespace std::chrono_literals;

namespace {
constexpr uint32_t kBase = 0x815F0000;

uint32_t Word(const std::vector<uint8_t>& image, uint32_t address) {
  const size_t o = address - kBase;
  REQUIRE(o + 4 <= image.size());
  return uint32_t(image[o]) << 24 | uint32_t(image[o + 1]) << 16 | uint32_t(image[o + 2]) << 8 |
         image[o + 3];
}
}  // namespace

TEST_CASE("17559 xam.xex avatar static initializer block", "[local][avatar]") {
  const fs::path root = fs::path(X360N_SOURCE_ROOT) /
                        ".local/inputs/dashboard/versions/17559/retail_system_assets/extracted";
  if (!fs::exists(root / "xam.xex")) SKIP("not present: " + (root / "xam.xex").string());
  const fs::path dir = UniqueTempPath("avatar_static_init_test");
  fs::remove_all(dir);
  fs::create_directories(dir);
  const auto out = dir / "xam.bin";
  auto result = RunProcess({X360N_CONTENT_EXE, {"dump-image", root.string(), "xam.xex", out.string()},
                            dir, dir / "out.log", dir / "err.log", 120s});
  REQUIRE(result.exit_code == 0);
  std::ifstream in(out, std::ios::binary);
  const std::vector<uint8_t> image{std::istreambuf_iterator<char>(in), {}};
  in.close();
  REQUIRE(image.size() == 0x610000);

  using namespace x360n::avatar;
  // The block sits inside the C++ initializer table, after an unrelated entry.
  CHECK(Word(image, kXam17559AvatarInitializerTable - 4) == 0x81A70E28);
  for (size_t i = 0; i < kXam17559AvatarInitializers.size(); ++i)
    CHECK(Word(image, kXam17559AvatarInitializerTable + uint32_t(4 * i)) ==
          kXam17559AvatarInitializers[i]);
  CHECK(Word(image, kXam17559AvatarInitializerTable + 36) == 0x81A71020);
  // Image values before initialization: only the X axis scale is non-zero.
  for (uint32_t scales : kXam17559SkeletonAxisScales) {
    CHECK(Word(image, scales) == 0x40000000);
    CHECK(Word(image, scales + 4) == 0);
    CHECK(Word(image, scales + 8) == 0);
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
}
