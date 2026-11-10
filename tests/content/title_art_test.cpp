// A synthetic basic-compressed XEX2 carrying its own SPA: the extractor reads its
// title name and icon, and the importer names a disc that has no art package.
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../../src/content/catalog.h"
#include "../../src/content/xex_crypto.h"
#include "../supervisor/scratch.h"

using namespace x360n;
using namespace x360n::desktop;
namespace fs = std::filesystem;
namespace {
using Bytes = std::vector<uint8_t>;
void Put32(Bytes& b, size_t o, uint32_t v) { for (unsigned i = 0; i < 4; ++i) b.at(o + i) = uint8_t(v >> (24 - 8 * i)); }
void Put16(Bytes& b, size_t o, uint16_t v) { b.at(o) = uint8_t(v >> 8); b.at(o + 1) = uint8_t(v); }
void Append(Bytes& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }
const std::string kPng = std::string("\x89PNG\r\n\x1A\n", 8) + "title icon";

Bytes Spa(const std::string& name) {
  Bytes xstc(16); Put32(xstc, 0, 0x58535443); Put32(xstc, 4, 1); Put32(xstc, 8, 12); Put32(xstc, 12, 1);
  Bytes xstr(18); Put32(xstr, 0, 0x58535452); Put32(xstr, 4, 1); Put32(xstr, 8, uint32_t(14 + name.size()));
  Put16(xstr, 12, 1); Put16(xstr, 14, 0x8000); Put16(xstr, 16, uint16_t(name.size())); Append(xstr, name);
  Bytes db(24 + 3 * 18); Put32(db, 0, 0x58444246); Put32(db, 4, 0x10000); Put32(db, 8, 3); Put32(db, 12, 3);
  const auto entry = [&](size_t i, uint16_t section, uint32_t id, uint32_t offset, size_t size) {
    Put16(db, 24 + 18 * i, section); Put32(db, 24 + 18 * i + 6, id);
    Put32(db, 24 + 18 * i + 10, offset); Put32(db, 24 + 18 * i + 14, uint32_t(size));
  };
  entry(0, 1, 0x58535443, 0, xstc.size());
  entry(1, 3, 1, uint32_t(xstc.size()), xstr.size());
  entry(2, 2, 0x8000, uint32_t(xstc.size() + xstr.size()), kPng.size());
  db.insert(db.end(), xstc.begin(), xstc.end()); db.insert(db.end(), xstr.begin(), xstr.end()); Append(db, kPng);
  return db;
}
Bytes Sha1(const Bytes& b) { const auto d = x360n::content::crypto::Sha1(b.data(), b.size()); return {d.begin(), d.end()}; }
// 0x1000 header, two 4 KiB pages, one basic data block; the SPA sits in page 1.
Bytes Xex(const Bytes& spa, bool corrupt_page = false) {
  constexpr size_t kSec = 0x100, kFfi = 0x40, kRes = 0x60, kExec = 0x80;
  constexpr uint32_t kBase = 0x82000000, kTitle = 0x12345678;
  Bytes h(0x1000), image(0x2000);
  for (size_t i = 0; i < 0x1000; ++i) image[i] = uint8_t(i * 7 + 3);
  std::copy(spa.begin(), spa.end(), image.begin() + 0x1000);
  Put32(h, 0, 0x58455832); Put32(h, 8, 0x1000); Put32(h, 0x10, kSec); Put32(h, 0x14, 3);
  Put32(h, 0x18, 0x3FF); Put32(h, 0x1C, kFfi); Put32(h, 0x20, 0x2FF); Put32(h, 0x24, kRes);
  Put32(h, 0x28, 0x40006); Put32(h, 0x2C, kExec);
  Put32(h, kFfi, 16); Put16(h, kFfi + 4, 0); Put16(h, kFfi + 6, 1); Put32(h, kFfi + 8, 0x2000);
  Put32(h, kRes, 20); for (int i = 0; i < 8; ++i) h[kRes + 4 + i] = uint8_t("12345678"[i]);
  Put32(h, kRes + 12, kBase + 0x1000); Put32(h, kRes + 16, uint32_t(spa.size()));
  Put32(h, kExec + 0xC, kTitle);
  Put32(h, kSec, 0x184 + 48); Put32(h, kSec + 4, 0x2000); Put32(h, kSec + 0x10C, 0x10000000); Put32(h, kSec + 0x110, kBase);
  Put32(h, kSec + 0x180, 2); Put32(h, kSec + 0x184, 0x11); Put32(h, kSec + 0x19C, 0x12);
  Bytes last(image.begin() + 0x1000, image.end()); last.insert(last.end(), h.begin() + kSec + 0x19C, h.begin() + kSec + 0x1B4);
  const auto d1 = Sha1(last); std::copy(d1.begin(), d1.end(), h.begin() + kSec + 0x188);
  Bytes first(image.begin(), image.begin() + 0x1000); first.insert(first.end(), h.begin() + kSec + 0x184, h.begin() + kSec + 0x19C);
  const auto d0 = Sha1(first); std::copy(d0.begin(), d0.end(), h.begin() + kSec + 0x114);
  if (corrupt_page) image[0x1FFF] ^= 1;
  h.insert(h.end(), image.begin(), image.end());
  return h;
}
void Write(const fs::path& p, const Bytes& b) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
std::string Read(const fs::path& p) { std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
}  // namespace

TEST_CASE("discs without art take their name and icon from the XEX's own SPA", "[universal][title-art]") {
  const auto root = UniqueTempPath("title_art");
  const ProjectPaths paths{root};
  bool corrupt = false; std::string expected = "Synthetic Racer";
  SECTION("verified image") {}
  SECTION("image that fails its page digests stays unnamed") { corrupt = true; expected.clear(); }
  Write(root / "source/default.xex", Xex(Spa("Synthetic Racer"), corrupt));
  ImportGame(paths, {}, root / "source", root / "source", {}, false, X360N_CONTENT_EXE);
  const auto games = ReadCatalog(paths);
  REQUIRE(games.size() == 1);
  CHECK(games[0].label.starts_with(expected.empty() ? "Xbox 360 12345678" : expected));
  const auto icon = ReadThumbnail(games[0]);
  CHECK(std::string(icon.begin(), icon.end()) == (expected.empty() ? "" : kPng));
  CHECK_FALSE(fs::exists(root / ".local/title-art"));
  std::error_code ec; fs::remove_all(root, ec);
}
