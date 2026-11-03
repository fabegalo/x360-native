#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "../../src/content/extraction.h"
#include "../../src/content/extraction_manifest.h"
#include "../supervisor/scratch.h"

namespace fs = std::filesystem;
namespace {
struct Scratch {
  fs::path dir = UniqueTempPath("extraction_manifest_test");
  Scratch() { fs::create_directories(dir); }
  ~Scratch() { std::error_code ec; fs::remove_all(dir, ec); }
};
std::string Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST_CASE("extraction manifest publishes complete bytes without replacement", "[content]") {
  Scratch s;
  const auto final = s.dir / "out.sha256";
  {
    x360n::content::ExtractionManifest manifest(final);
    manifest.stream() << "complete\n";
    CHECK_FALSE(fs::exists(final));
    SECTION("success") {
      manifest.Publish();
      CHECK(Read(final) == "complete\n");
    }
    SECTION("late destination collision") {
      std::ofstream(final) << "sentinel";
      CHECK_THROWS(manifest.Publish());
      CHECK(Read(final) == "sentinel");
    }
    SECTION("late directory collision") {
      fs::create_directory(final);
      CHECK_THROWS(manifest.Publish());
      CHECK(fs::is_directory(final));
    }
    SECTION("write failure") {
      // Models a failed stream write (e.g. disk full), including prior bytes.
      manifest.stream().setstate(std::ios::badbit);
      CHECK_THROWS(manifest.Publish());
      CHECK_FALSE(fs::exists(final));
    }
    SECTION("abandoned extraction") {
      // Destruction without Publish must not expose a final manifest.
      CHECK_FALSE(fs::exists(final));
    }
  }
  CHECK_FALSE(fs::exists(s.dir / "out.sha256.partial"));
}

TEST_CASE("extraction manifest preserves occupied temporary files and links", "[content]") {
  Scratch s;
  const auto final = s.dir / "out.sha256";
  const auto partial = s.dir / "out.sha256.partial";
  SECTION("file") {
    std::ofstream(partial) << "sentinel";
    CHECK_THROWS(x360n::content::ExtractionManifest(final));
    CHECK(Read(partial) == "sentinel");
  }
  SECTION("directory") {
    fs::create_directory(partial);
    CHECK_THROWS(x360n::content::ExtractionManifest(final));
    CHECK(fs::is_directory(partial));
  }
  SECTION("hard link") {
    std::ofstream(s.dir / "original") << "sentinel";
    fs::create_hard_link(s.dir / "original", partial);
    CHECK_THROWS(x360n::content::ExtractionManifest(final));
    CHECK(Read(s.dir / "original") == "sentinel");
  }
  CHECK_FALSE(fs::exists(final));
}
// Forza Horizon disc 1 (2432 files, 7,320,461,527 bytes) holds a 2,942,369,996
// byte file; a 2 GiB limit per file refused its import.
TEST_CASE("extraction limits accept retail disc files beyond 2 GiB", "[content]") {
  using x360n::content::WithinExtractionLimits;
  CHECK(WithinExtractionLimits(2432, 2942369996ull, 7320461527ull - 2942369996ull));
  CHECK(WithinExtractionLimits(1, 0xFFFFFFFFull, 0));
  CHECK_FALSE(WithinExtractionLimits(1, 0x100000000ull, 0));
  CHECK_FALSE(WithinExtractionLimits(65537, 1, 0));
  CHECK_FALSE(WithinExtractionLimits(3, 3000000000ull, 6000000000ull));
  CHECK(WithinExtractionLimits(3, (8ull << 30) - 6000000000ull, 6000000000ull));
}
