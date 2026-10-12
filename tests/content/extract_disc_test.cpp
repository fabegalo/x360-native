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
    return RunProcess({X360N_CONTENT_EXE,
                       {"extract-disc", image.string(), dest.string()},
                       dir, dir / "out.log", dir / "err.log", 60s});
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

TEST_CASE("extract-disc refuses file ranges outside the image", "[content]") {
  Scratch s;
  // 35 sectors = 71680 bytes: far.bin starts beyond the end, long.bin runs past it.
  auto img = MakeImage({{"ok.bin", 34, 5}, {"far.bin", 1000, 16}, {"long.bin", 34, 4096}}, 35);
  auto r = s.Extract(s.Write(img), s.dir / "out");
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(fs::exists(s.dir / "out" / "ok.bin"));
  CHECK_FALSE(fs::exists(s.dir / "out" / "far.bin"));
  CHECK_FALSE(fs::exists(s.dir / "out" / "long.bin"));
  CHECK(s.Err().find("far.bin lies outside the image") != std::string::npos);
  CHECK(s.Err().find("long.bin lies outside the image") != std::string::npos);
}

TEST_CASE("extract-disc refuses unsafe names", "[content]") {
  Scratch s;
  auto r = s.Extract(s.Write(MakeImage({{"..", 34, 5}}, 35)), s.dir / "out");
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(s.Err().find("refusing unsafe entry name '..'") != std::string::npos);
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
