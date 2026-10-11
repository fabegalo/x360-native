// XEX2 header parsing against SYNTHETIC headers built here. They follow the
// layouts in the SDK's xex2_info.h but are not real executables and say
// nothing about real-title support.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "child_process.h"
#include "scratch.h"
#include "xex_identity.h"

using namespace x360n;

namespace {

uint32_t Ver(uint32_t major, uint32_t minor, uint32_t build, uint32_t qfe) {
  return (major << 28) | (minor << 24) | (build << 8) | qfe;
}

struct Builder {
  std::vector<uint8_t> b;
  void Put32(size_t off, uint32_t v) {
    if (b.size() < off + 4) b.resize(off + 4);
    b[off] = uint8_t(v >> 24); b[off + 1] = uint8_t(v >> 16);
    b[off + 2] = uint8_t(v >> 8); b[off + 3] = uint8_t(v);
  }
  void Put16(size_t off, uint16_t v) {
    if (b.size() < off + 2) b.resize(off + 2);
    b[off] = uint8_t(v >> 8); b[off + 1] = uint8_t(v);
  }
  void Put8(size_t off, uint8_t v) {
    if (b.size() < off + 1) b.resize(off + 1);
    b[off] = v;
  }
  size_t Append(size_t size) {  // 8-aligned block at the end
    size_t off = (b.size() + 7) & ~size_t(7);
    b.resize(off + size);
    return off;
  }
};

struct Synthetic {
  std::vector<uint8_t> bytes;
  size_t exec_entry = 0;   // offset of the EXECUTION_INFO opt-header entry
  size_t import_block = 0; // offset of the import libraries block
};

Synthetic MakeSynthetic(uint32_t title_id = 0x4D530001) {
  Builder x;
  const uint32_t count = 6;
  x.Put32(0x00, 0x58455832);  // XEX2
  x.Put32(0x04, 0x00000001);  // title
  x.Put32(0x0C, 0);
  x.Put32(0x14, count);
  x.b.resize(0x18 + count * 8);

  size_t exec = x.Append(0x18);
  x.Put32(exec + 0x00, 0xAABBCCDD);              // media id
  x.Put32(exec + 0x04, Ver(1, 2, 300, 4));       // version
  x.Put32(exec + 0x08, Ver(1, 0, 0, 0));         // base version
  x.Put32(exec + 0x0C, title_id);
  x.Put8(exec + 0x12, 1);                        // disc 1
  x.Put8(exec + 0x13, 2);                        // of 2
  x.Put32(exec + 0x14, 0x12345678);              // savegame id

  const std::string pe = "synthetic.exe";
  size_t pen = x.Append(4 + pe.size() + 1);
  x.Put32(pen, uint32_t(4 + pe.size() + 1));
  for (size_t i = 0; i < pe.size(); ++i) x.Put8(pen + 4 + i, uint8_t(pe[i]));

  size_t ffi = x.Append(8);
  x.Put32(ffi, 8);
  x.Put16(ffi + 4, 1);  // encryption normal
  x.Put16(ffi + 6, 2);  // compression normal

  // Import libraries: string table "xboxkrnl.exe\0" (13 -> pad 16) "xam.xex\0" (8)
  const std::string s1 = "xboxkrnl.exe", s2 = "xam.xex";
  uint32_t table_size = 16 + 8;
  uint32_t lib1 = 0x28 + 4 * 3, lib2 = 0x28 + 4 * 1;
  uint32_t total = 12 + table_size + lib1 + lib2;
  size_t imp = x.Append(total);
  x.Put32(imp, total);
  x.Put32(imp + 4, table_size);
  x.Put32(imp + 8, 2);
  for (size_t i = 0; i < s1.size(); ++i) x.Put8(imp + 12 + i, uint8_t(s1[i]));
  for (size_t i = 0; i < s2.size(); ++i) x.Put8(imp + 12 + 16 + i, uint8_t(s2[i]));
  size_t l = imp + 12 + table_size;
  x.Put32(l, lib1);
  x.Put32(l + 0x1C, Ver(2, 0, 17559, 0));
  x.Put32(l + 0x20, Ver(2, 0, 1888, 0));
  x.Put16(l + 0x24, 0);  // name index -> xboxkrnl.exe
  x.Put16(l + 0x26, 3);
  l += lib1;
  x.Put32(l, lib2);
  x.Put32(l + 0x1C, Ver(2, 0, 17559, 0));
  x.Put32(l + 0x20, Ver(2, 0, 17559, 0));
  x.Put16(l + 0x24, 1);  // -> xam.xex
  x.Put16(l + 0x26, 1);

  size_t sec = x.Append(0x184 + 0x18);
  x.Put32(sec + 0x004, 0x00400000);  // image size
  x.Put32(sec + 0x110, 0x82000000);  // load address
  x.Put32(sec + 0x178, 0xFFFFFFFF);  // region

  const uint32_t keys[count] = {0x00010100, 0x00010201, 0x00040006, 0x000183FF, 0x000003FF, 0x000103FF};
  const uint32_t vals[count] = {0x82000100, 0x82000000, uint32_t(exec), uint32_t(pen), uint32_t(ffi), uint32_t(imp)};
  for (uint32_t i = 0; i < count; ++i) {
    x.Put32(0x18 + i * 8, keys[i]);
    x.Put32(0x18 + i * 8 + 4, vals[i]);
  }
  x.Put32(0x10, uint32_t(sec));
  x.Put32(0x08, uint32_t(x.b.size()));  // header_size covers everything above
  return {x.b, 0x18 + 2 * 8, imp};
}

}  // namespace

TEST_CASE("parses identity fields from a synthetic XEX2 header", "[xex]") {
  auto s = MakeSynthetic();
  auto r = ParseXexIdentity(s.bytes);
  REQUIRE(r.identity);
  const auto& id = *r.identity;
  CHECK(id.warnings.empty());
  CHECK(id.module_flags == 1);
  REQUIRE(id.has_execution_info);
  CHECK(id.title_id == 0x4D530001);
  CHECK(id.media_id == 0xAABBCCDD);
  CHECK(id.version.ToString() == "1.2.300.4");
  CHECK(id.base_version.ToString() == "1.0.0.0");
  CHECK(id.disc_number == 1);
  CHECK(id.disc_count == 2);
  CHECK(id.savegame_id == 0x12345678);
  CHECK(id.entry_point == 0x82000100u);
  CHECK(id.image_base == 0x82000000u);
  CHECK(id.original_pe_name == "synthetic.exe");
  CHECK(id.encryption_type == 1);
  CHECK(id.compression_type == 2);
  CHECK(id.image_size == 0x00400000u);
  CHECK(id.load_address == 0x82000000u);
  CHECK(id.region == 0xFFFFFFFFu);
  REQUIRE(id.imports.size() == 2);
  CHECK(id.imports[0].name == "xboxkrnl.exe");
  CHECK(id.imports[0].version.ToString() == "2.0.17559.0");
  CHECK(id.imports[0].version_min.ToString() == "2.0.1888.0");
  CHECK(id.imports[0].table_entries == 3);
  CHECK(id.imports[1].name == "xam.xex");
  CHECK(id.imports[1].table_entries == 1);
  CHECK_FALSE(IsDashboardTitleId(id.title_id));
}

TEST_CASE("flags the dashboard title id", "[xex]") {
  auto r = ParseXexIdentity(MakeSynthetic(0xFFFE07D1).bytes);
  REQUIRE(r.identity);
  CHECK(IsDashboardTitleId(r.identity->title_id));
}

TEST_CASE("names module flags and keeps unknown bits visible", "[xex]") {
  auto names = DescribeModuleFlags(0x00000049 | 0x00010000);
  REQUIRE(names.size() == 4);
  CHECK(names[0] == "title");
  CHECK(names[1] == "dll_module");
  CHECK(names[2] == "patch_delta");
  CHECK(names[3] == "unknown_0x00010000");
}

TEST_CASE("rejects inputs that are not a valid XEX2 header block", "[xex]") {
  auto s = MakeSynthetic();
  CHECK_FALSE(ParseXexIdentity(std::span(s.bytes).first(0x10)).identity);  // truncated

  auto bad_magic = s.bytes;
  bad_magic[0] = 'Z';
  CHECK_FALSE(ParseXexIdentity(bad_magic).identity);

  // header_size claims more than was provided
  CHECK_FALSE(ParseXexIdentity(std::span(s.bytes).first(s.bytes.size() - 1)).identity);

  auto huge_count = s.bytes;
  huge_count[0x14] = huge_count[0x15] = huge_count[0x16] = huge_count[0x17] = 0xFF;
  auto r = ParseXexIdentity(huge_count);
  CHECK_FALSE(r.identity);
  CHECK(r.error.find("header_count") != std::string::npos);
  CHECK(r.format == XexFormat::kXex2);  // a malformed XEX2 is still classified as XEX2
}

TEST_CASE("classifies older XEX signatures apart from corrupt input", "[xex]") {
  auto s = MakeSynthetic();
  auto ok = ParseXexIdentity(s.bytes);
  CHECK(ok.format == XexFormat::kXex2);
  CHECK(ok.identity);
  CHECK(ok.signature == "XEX2");

  auto with_magic = [&](const char* m) {
    auto v = s.bytes;
    for (int i = 0; i < 4; ++i) v[size_t(i)] = uint8_t(m[i]);
    return ParseXexIdentity(v);
  };
  auto x1 = with_magic("XEX1");
  CHECK(x1.format == XexFormat::kXex1);
  CHECK_FALSE(x1.identity);  // recognized, not read
  CHECK(x1.error.find("XEX1") != std::string::npos);
  for (const char* m : {"XEX0", "XEX?", "XEX-", "XEX%"}) {
    INFO(m);
    auto v = with_magic(m);
    CHECK(v.format == XexFormat::kXexOtherVariant);
    CHECK_FALSE(v.identity);
    CHECK(v.signature == m);
  }

  // An XEX2 header that lost its first byte: invalid, with the hint.
  std::vector<uint8_t> shifted(s.bytes.begin() + 1, s.bytes.end());
  auto sh = ParseXexIdentity(shifted);
  CHECK(sh.format == XexFormat::kInvalid);
  CHECK(sh.error.find("missing its first byte") != std::string::npos);

  auto zip = with_magic("PK\x03\x04");
  CHECK(zip.format == XexFormat::kInvalid);
  CHECK(zip.signature == "PK..");
  CHECK(ParseXexIdentity(std::span(s.bytes).first(3)).format == XexFormat::kInvalid);
  // Too short but with the XEX2 signature: a malformed XEX2, not something else.
  CHECK(ParseXexIdentity(std::span(s.bytes).first(0x10)).format == XexFormat::kXex2);
}

TEST_CASE("skips malformed optional headers with a warning", "[xex]") {
  auto s = MakeSynthetic();
  auto b = s.bytes;
  // EXECUTION_INFO offset beyond the header block
  b[s.exec_entry + 4] = 0x7F;
  auto r = ParseXexIdentity(b);
  REQUIRE(r.identity);
  CHECK_FALSE(r.identity->has_execution_info);
  CHECK_FALSE(r.identity->warnings.empty());

  // First import library record claims an enormous size
  auto c = s.bytes;
  size_t lib = s.import_block + 12 + 24;
  c[lib] = c[lib + 1] = c[lib + 2] = c[lib + 3] = 0xFF;
  r = ParseXexIdentity(c);
  REQUIRE(r.identity);
  CHECK(r.identity->imports.empty());
  CHECK_FALSE(r.identity->warnings.empty());
}

TEST_CASE("x360n identify --json reads the header from a file", "[cli]") {
  namespace fs = std::filesystem;
  fs::path dir = UniqueTempPath("identify_cli");
  fs::remove_all(dir);
  fs::create_directories(dir);
  auto s = MakeSynthetic(0xFFFE07D1);
  s.bytes.resize(s.bytes.size() + 4096, 0xAB);  // trailing "image" bytes beyond header_size
  { std::ofstream(dir / "synthetic.xex", std::ios::binary).write(reinterpret_cast<const char*>(s.bytes.data()), std::streamsize(s.bytes.size())); }

  ProcessSpec spec{X360N_EXE, {"identify", "--json", (dir / "synthetic.xex").string()}, dir,
                   dir / "out.json", dir / "err.txt", std::chrono::seconds(60)};
  auto r = RunProcess(spec);
  REQUIRE(r.launched);
  CHECK(r.exit_code == 0);
  std::ifstream in(dir / "out.json", std::ios::binary);
  std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::erase(json, '\r');  // stdout is in text mode on Windows: "\n" arrives as "\r\n"
  CHECK(json.find("\"format\": \"xex2\"") != std::string::npos);
  CHECK(json.find("\"title_id\": \"FFFE07D1\"") != std::string::npos);
  CHECK(json.find("\"is_dashboard_title_id\": true") != std::string::npos);
  CHECK(json.find("\"name\": \"xam.xex\"") != std::string::npos);
  CHECK(json.find("\"warnings\": [\n    ]") != std::string::npos);
  in.close();
  fs::remove_all(dir);
}

TEST_CASE("random corruption never reads out of bounds or throws", "[xex]") {
  auto base = MakeSynthetic().bytes;
  std::mt19937 rng(0x360);
  for (int iter = 0; iter < 20000; ++iter) {
    auto b = base;
    int flips = 1 + int(rng() % 8);
    for (int f = 0; f < flips; ++f) b[rng() % b.size()] = uint8_t(rng());
    size_t len = (iter % 7 == 0) ? rng() % b.size() : b.size();
    auto r = ParseXexIdentity(std::span(b).first(len));
    CHECK((r.identity.has_value() || !r.error.empty()));
  }
}
