// x360n-content extract-disc over synthetic GDFX images (no game content). The
// images follow what the SDK's DiscImageDevice reads (disc_image_device.cpp):
// "MICROSOFT*XBOX*MEDIA" at sector 32 of the game partition (offset 0 here), root
// directory sector and size at +20/+24, and directory entries {left u16, right u16,
// sector u32, length u32, attributes u8, name length u8, name}, little-endian.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../supervisor/scratch.h"
#include "child_process.h"
#include "paths.h"

namespace fs = std::filesystem;
using namespace x360n;
using namespace std::chrono_literals;

namespace {

constexpr size_t kSector = 2048;
constexpr uint32_t kRootSector = 33;

struct DiscFile {
  std::string name;
  uint32_t sector;
  uint32_t length;
};

void Put16(std::vector<uint8_t>& b, size_t o, uint16_t v) {
  b[o] = uint8_t(v);
  b[o + 1] = uint8_t(v >> 8);
}

void Put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[o + i] = uint8_t(v >> (8 * i));
}

// One root directory holding `files` as a right-linked list.
std::vector<uint8_t> MakeImage(const std::vector<DiscFile>& files, size_t sectors) {
  std::vector<uint8_t> img(sectors * kSector, 0);
  const size_t volume = 32 * kSector;
  std::memcpy(&img[volume], "MICROSOFT*XBOX*MEDIA", 20);
  Put32(img, volume + 20, kRootSector);
  Put32(img, volume + 24, uint32_t(kSector));
  size_t o = kRootSector * kSector;
  for (size_t i = 0; i < files.size(); ++i) {
    const auto& f = files[i];
    const size_t len = (14 + f.name.size() + 3) & ~size_t(3);
    const bool last = i + 1 == files.size();
    Put16(img, o + 0, 0);
    Put16(img, o + 2, last ? 0 : uint16_t((o + len - kRootSector * kSector) / 4));
    Put32(img, o + 4, f.sector);
    Put32(img, o + 8, f.length);
    img[o + 12] = 0x80;  // kFileAttributeNormal
    img[o + 13] = uint8_t(f.name.size());
    std::memcpy(&img[o + 14], f.name.data(), f.name.size());
    o += len;
  }
  return img;
}

std::string Slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Scratch {
  fs::path dir = UniqueTempPath("extract_disc_test");
  Scratch() {
    fs::remove_all(dir);
    fs::create_directories(dir);
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  fs::path Write(const std::vector<uint8_t>& img) const {
    fs::path p = dir / "disc.iso";
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(img.data()),
                                             std::streamsize(img.size()));
    return p;
  }
  ProcessResult Extract(const fs::path& image, const fs::path& dest) const {
    auto result = RunProcess({X360N_CONTENT_EXE,
                       {"extract-disc", image.string(), dest.string()},
                       dir, dir / "out.log", dir / "err.log", 10s});
    REQUIRE(result.launched);
    REQUIRE_FALSE(result.timed_out);
    return result;
  }
  std::string Err() const { return Slurp(dir / "err.log"); }
};

}  // namespace

TEST_CASE("extract-disc copies files and writes the manifest beside the destination",
          "[content]") {
  Scratch s;
  auto img = MakeImage({{"hello.txt", 34, 5}}, 35);
  std::memcpy(&img[34 * kSector], "hello", 5);
  auto r = s.Extract(s.Write(img), s.dir / "out");
  REQUIRE(r.launched);
  CHECK(r.exit_code == 0);
  CHECK(Slurp(s.dir / "out" / "hello.txt") == "hello");
  CHECK(Slurp(s.dir / "out.sha256") ==
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 *hello.txt\n");
}

TEST_CASE("extract-disc writes beyond MAX_PATH in a clean Windows installation", "[content]") {
  Scratch s;
  auto img = MakeImage({{"long.bin", 34, 5}}, 35);
  std::memcpy(&img[34 * kSector], "hello", 5);
  const auto source = s.Write(img);
  // A release may live in an arbitrary user folder. Its logs/extraction/content
  // tree and a deeply nested guest path must not require LongPathsEnabled.
  auto nested = s.dir / "nested";
  fs::create_directories(LongPath(nested));
  while ((nested / "out" / "long.bin").native().size() <= 270) {
    nested /= std::string(35, 'd');
    fs::create_directory(LongPath(nested));
  }
  auto output = nested / "out";
  const auto result = s.Extract(source, output);
  INFO(s.Err());
  REQUIRE(result.exit_code == 0);
  CHECK(Slurp(LongPath(output / "long.bin")) == "hello");
  CHECK(Slurp(LongPath(fs::path(output.native() + fs::path(".sha256").native()))).find(" *long.bin") != std::string::npos);
  // Scratch removes the parent tree. Long-path cleanup is intentionally
  // explicit here since Windows directory traversal depends on API policy.
  fs::remove_all(LongPath(s.dir / "nested"));
}

TEST_CASE("extract-disc refuses file ranges outside the image", "[content]") {
  Scratch s;
  // 35 sectors = 71680 bytes: far.bin starts beyond the end, long.bin runs past it.
  auto img = MakeImage({{"ok.bin", 34, 5}, {"far.bin", 1000, 16}, {"long.bin", 34, 4096}}, 35);
  auto r = s.Extract(s.Write(img), s.dir / "out");
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  // The SDK rejects the entire malformed filesystem before exposing entries.
  CHECK_FALSE(fs::exists(s.dir / "out" / "ok.bin"));
  CHECK_FALSE(fs::exists(s.dir / "out" / "far.bin"));
  CHECK_FALSE(fs::exists(s.dir / "out" / "long.bin"));
  CHECK(s.Err().find("DiscImageDevice::Initialize failed") != std::string::npos);
  CHECK_FALSE(fs::exists(s.dir / "out.sha256"));
}

TEST_CASE("extract-disc refuses unsafe names", "[content]") {
  Scratch s;
  auto r = s.Extract(s.Write(MakeImage({{"..", 34, 5}}, 35)), s.dir / "out");
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(s.Err().find("refusing unsafe entry name '..'") != std::string::npos);
  CHECK_FALSE(fs::exists(s.dir / "out.sha256"));
}

TEST_CASE("extract-disc rejects malformed GDFX without crashing or looping", "[content]") {
  Scratch s;
  auto img = MakeImage({{"a", 34, 1}, {"b", 34, 1}, {"c", 34, 1}}, 35);
  constexpr size_t v = 32 * kSector, r = kRootSector * kSector;
  SECTION("truncated magic") { img.resize(v + 19); }
  SECTION("truncated descriptor") { img.resize(v + 24); }
  SECTION("audit root sector") { Put32(img, v + 20, 2097152); }
  SECTION("maximum root sector") { Put32(img, v + 20, UINT32_MAX); }
  SECTION("root span beyond EOF") { Put32(img, v + 24, 3 * kSector); }
  SECTION("short root header") { Put32(img, v + 24, 13); }
  SECTION("name outside declared directory") { Put32(img, v + 24, 14); }
  SECTION("empty name") { img[r + 13] = 0; }
  SECTION("embedded NUL name") { img[r + 14] = 0; }
  SECTION("child ordinal outside table") { Put16(img, r + 2, UINT16_MAX); }
  SECTION("entry header crosses table end") { Put16(img, r + 2, (kSector - 4) / 4); }
  SECTION("overlapping records") { Put16(img, r + 2, 1); }
  SECTION("sibling self cycle") { Put16(img, r + 16 + 2, 4); }
  SECTION("two sibling cycle") { Put16(img, r + 32 + 2, 4); }
  SECTION("left sibling cycle") { Put16(img, r + 16, 4); }
  SECTION("directory points to root") {
    img[r + 12] = 0x10; Put32(img, r + 4, kRootSector); Put32(img, r + 8, kSector);
  }
  SECTION("directory starts at EOF") {
    img[r + 12] = 0x10; Put32(img, r + 4, 35); Put32(img, r + 8, 16);
  }
  SECTION("directory span exceeds EOF") {
    img[r + 12] = 0x10; Put32(img, r + 8, 2 * kSector);
  }
  SECTION("two directory cycle") {
    img[r + 12] = 0x10; Put32(img, r + 8, 16);
    const size_t c = 34 * kSector;
    Put32(img, c + 4, 33); Put32(img, c + 8, kSector);
    img[c + 12] = 0x10; img[c + 13] = 1; img[c + 14] = 'd';
  }
  SECTION("directory shorter than entry") {
    img[r + 12] = 0x10; Put32(img, r + 8, 13);
  }
  SECTION("file maximum offset") { Put32(img, r + 4, UINT32_MAX); }
  SECTION("file maximum length") { Put32(img, r + 8, UINT32_MAX); }
  SECTION("file span exceeds EOF") { Put32(img, r + 8, kSector + 1); }
  auto result = s.Extract(s.Write(img), s.dir / "out");
  REQUIRE(result.launched);
  CHECK_FALSE(result.timed_out);
  CHECK(result.exit_code == 1);
  CHECK(s.Err().find("DiscImageDevice::Initialize failed") != std::string::npos);
  CHECK_FALSE(fs::exists(s.dir / "out"));
  CHECK_FALSE(fs::exists(s.dir / "out.sha256"));
}

TEST_CASE("extract-disc preserves existing sidecars and rejects output errors", "[content]") {
  Scratch s;
  auto image = s.Write(MakeImage({{"a", 34, 1}}, 35));
  SECTION("existing manifest") {
    std::ofstream(s.dir / "out.sha256") << "sentinel";
    auto result = s.Extract(image, s.dir / "out");
    CHECK(result.exit_code == 3);
    CHECK(Slurp(s.dir / "out.sha256") == "sentinel");
    CHECK_FALSE(fs::exists(s.dir / "out"));
  }
  SECTION("manifest is directory") {
    fs::create_directory(s.dir / "out.sha256");
    auto result = s.Extract(image, s.dir / "out");
    CHECK(result.exit_code == 3);
    CHECK(fs::is_directory(s.dir / "out.sha256"));
  }
  SECTION("parent is a file") {
    std::ofstream(s.dir / "parent") << "sentinel";
    auto result = s.Extract(image, s.dir / "parent" / "out");
    CHECK((result.exit_code == 1 || result.exit_code == 3));
    CHECK(Slurp(s.dir / "parent") == "sentinel");
  }
  SECTION("occupied staging name") {
    std::ofstream(s.dir / "out.sha256.partial") << "sentinel";
    auto result = s.Extract(image, s.dir / "out");
    CHECK(result.exit_code == 1);
    CHECK(Slurp(s.dir / "out.sha256.partial") == "sentinel");
    CHECK_FALSE(fs::exists(s.dir / "out.sha256"));
    CHECK_FALSE(fs::exists(s.dir / "out"));
  }
}

TEST_CASE("extract-disc bounds deep directories but accepts long sibling chains", "[content]") {
  Scratch s;
  SECTION("deep directory chain") {
    auto img = MakeImage({{"d", 34, 16}}, 300);
    for (size_t sector = 33; sector < 299; ++sector) {
      const size_t o = sector * kSector;
      Put32(img, o + 4, uint32_t(sector + 1)); Put32(img, o + 8, 16);
      img[o + 12] = 0x10; img[o + 13] = 1; img[o + 14] = 'd';
    }
    auto result = s.Extract(s.Write(img), s.dir / "out");
    CHECK(result.exit_code == 1);
    CHECK(s.Err().find("DiscImageDevice::Initialize failed") != std::string::npos);
    CHECK_FALSE(fs::exists(s.dir / "out"));
  }
  SECTION("300 siblings") {
    std::vector<DiscFile> files;
    for (int i = 0; i < 300; ++i) files.push_back({"f" + std::to_string(i), 0, 0});
    auto img = MakeImage(files, 40);
    Put32(img, 32 * kSector + 24, 4 * kSector);
    auto result = s.Extract(s.Write(img), s.dir / "out");
    REQUIRE(result.exit_code == 0);
    CHECK(std::distance(fs::directory_iterator(s.dir / "out"), fs::directory_iterator{}) == 300);
  }
}

TEST_CASE("extract-disc preserves partition-relative sectors and tree order", "[content]") {
  Scratch s;
  auto img = MakeImage({{"b", 0, 0}, {"a", 0, 0}, {"c", 0, 0}}, 35);
  Put16(img, 33 * kSector, 4); Put16(img, 33 * kSector + 2, 8);
  Put16(img, 33 * kSector + 16 + 2, 0);
  img.insert(img.begin(), 0xFB20, 0);
  auto result = s.Extract(s.Write(img), s.dir / "out");
  REQUIRE(result.exit_code == 0);
  auto manifest = Slurp(s.dir / "out.sha256");
  CHECK(manifest.find(" *a\n") < manifest.find(" *b\n"));
  CHECK(manifest.find(" *b\n") < manifest.find(" *c\n"));
}

TEST_CASE("extract-disc normalizes trailing directory separators before publication", "[content]") {
  Scratch s;
  auto destination = s.dir / "out";
  SECTION("separator") { destination /= ""; }
  SECTION("dot") { destination /= "."; }
  auto result = s.Extract(s.Write(MakeImage({{"zero", 0, 0}}, 35)), destination);
  REQUIRE(result.exit_code == 0);
  CHECK(fs::is_regular_file(s.dir / "out" / "zero"));
  CHECK(fs::is_regular_file(s.dir / "out.sha256"));
  CHECK_FALSE(fs::exists(s.dir / "out" / ".sha256"));
  CHECK_FALSE(fs::exists(s.dir / "out.sha256.partial"));
}

TEST_CASE("extract-disc preserves nested empty directories and empty files", "[content]") {
  Scratch s;
  auto img = MakeImage({{"folder", 34, 16}, {"empty", 0, 0}, {"zero", 0, 0}}, 36);
  img[33 * kSector + 12] = 0x10;
  img[33 * kSector + 20 + 12] = 0x10;
  const size_t child = 34 * kSector;
  Put32(img, child + 4, 35); Put32(img, child + 8, 5);
  img[child + 12] = 0x80; img[child + 13] = 1; img[child + 14] = 'a';
  std::memcpy(&img[35 * kSector], "hello", 5);
  auto result = s.Extract(s.Write(img), s.dir / "out");
  REQUIRE(result.exit_code == 0);
  CHECK(Slurp(s.dir / "out" / "folder" / "a") == "hello");
  CHECK(fs::is_directory(s.dir / "out" / "empty"));
  CHECK(fs::file_size(s.dir / "out" / "zero") == 0);
}

TEST_CASE("extract-disc refuses an existing destination and a non-GDFX file", "[content]") {
  Scratch s;
  fs::create_directories(s.dir / "taken");
  auto r = s.Extract(s.Write(MakeImage({{"a.bin", 34, 1}}, 35)), s.dir / "taken");
  REQUIRE(r.launched);
  CHECK(r.exit_code == 3);

  auto blank = s.Write(std::vector<uint8_t>(35 * kSector, 0));
  r = s.Extract(blank, s.dir / "out");
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(s.Err().find("DiscImageDevice::Initialize failed") != std::string::npos);
  CHECK_FALSE(fs::exists(s.dir / "out"));
}
