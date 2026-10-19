// Independent synthetic XEX2/XEXP fixtures. AES/SHA use Windows CNG and LZX
// uses stored blocks: no original executable, real wrapping key or SDK encoder.
#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "../supervisor/scratch.h"
#include "child_process.h"

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using Key = std::array<uint8_t, 16>;
constexpr size_t kHeader = 0x1000, kSec = 0x100, kFfi = 0x40, kDelta = 0x400;

void Put32(Bytes& b, size_t o, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) b.at(o + i) = uint8_t(v >> (24 - 8 * i));
}
void Put16(Bytes& b, size_t o, uint16_t v) {
  b.at(o) = uint8_t(v >> 8); b.at(o + 1) = uint8_t(v);
}
Bytes Hash(std::span<const uint8_t> bytes) {
  BCRYPT_ALG_HANDLE alg{};
  REQUIRE(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0);
  Bytes hash(20);
  const auto status = BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                                ULONG(bytes.size()), hash.data(), ULONG(hash.size()));
  BCryptCloseAlgorithmProvider(alg, 0);
  REQUIRE(status == 0);
  return hash;
}
Bytes Encrypt(const Key& key, std::span<const uint8_t> bytes) {
  BCRYPT_ALG_HANDLE alg{}; BCRYPT_KEY_HANDLE handle{};
  REQUIRE(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0);
  REQUIRE(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
      sizeof(BCRYPT_CHAIN_MODE_CBC), 0) == 0);
  REQUIRE(BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0,
      const_cast<PUCHAR>(key.data()), ULONG(key.size()), 0) == 0);
  Bytes output(bytes.size()); std::array<uint8_t, 16> iv{}; ULONG written{};
  const auto status = BCryptEncrypt(handle, const_cast<PUCHAR>(bytes.data()), ULONG(bytes.size()),
      nullptr, iv.data(), ULONG(iv.size()), output.data(), ULONG(output.size()), &written, 0);
  BCryptDestroyKey(handle); BCryptCloseAlgorithmProvider(alg, 0);
  REQUIRE(status == 0); REQUIRE(written == output.size());
  return output;
}
void Copy(Bytes& b, size_t at, std::span<const uint8_t> value) {
  REQUIRE(at <= b.size()); REQUIRE(value.size() <= b.size() - at);
  std::copy(value.begin(), value.end(), b.begin() + at);
}
Bytes StoredLzx(std::span<const uint8_t> bytes) {
  // Intel flag 0, block type 3, 24-bit length, word alignment. LZX bit
  // words are little endian, with bits consumed most significant first.
  const uint32_t bits = (3u << 28) | (uint32_t(bytes.size()) << 4);
  Bytes out{uint8_t(bits >> 16), uint8_t(bits >> 24), uint8_t(bits), uint8_t(bits >> 8)};
  for (unsigned i = 0; i < 3; ++i) out.insert(out.end(), {1, 0, 0, 0});
  out.insert(out.end(), bytes.begin(), bytes.end());
  if (out.size() % 2) out.push_back(0);
  return out;
}
Bytes Delta(uint32_t old, uint32_t dest, uint16_t length, uint16_t compressed,
            std::span<const uint8_t> data = {}) {
  Bytes out(12); Put32(out, 0, old); Put32(out, 4, dest);
  Put16(out, 8, length); Put16(out, 10, compressed);
  out.insert(out.end(), data.begin(), data.end());
  return out;
}
Bytes Read(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
void Write(const fs::path& p, const Bytes& bytes) {
  std::ofstream out(p, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  REQUIRE(out.good());
}

struct Fixture {
  fs::path dir = UniqueTempPath("apply_patch_synthetic");
  Key master{}, base_key{}, target_key{}, patch_key{};
  Bytes base_header = Bytes(kHeader), target_header, patch_header;
  Bytes base_image = Bytes(0x2000), target_image, base_body, records;
  bool chained = false, corrupt_patch_block = false;
  size_t base_tail = 0;  // trailing bytes after the last whole AES block
  explicit Fixture(bool chain = false) : chained(chain) {
    fs::create_directories(dir);
    for (unsigned i = 0; i < 16; ++i) {
      base_key[i] = uint8_t(0x10 + i); target_key[i] = uint8_t(0x30 + i);
      patch_key[i] = uint8_t(0x50 + i);
    }
    for (size_t i = 0; i < base_image.size(); ++i) base_image[i] = uint8_t(i * 7 + 3);
    Put32(base_header, 0, 0x58455832); Put32(base_header, 8, kHeader);
    Put32(base_header, 0x10, kSec); Put32(base_header, 0x14, 1);
    Put32(base_header, 0x18, 0x3FF); Put32(base_header, 0x1C, kFfi);
    Put32(base_header, kFfi, 0x24); Put16(base_header, kFfi + 4, 1);
    Put16(base_header, kFfi + 6, 2); Put32(base_header, kFfi + 8, 0x8000);
    Put32(base_header, kSec + 0x10C, 0x10000000);
    for (size_t i = 0; i < 0x100; ++i) base_header[kSec + 8 + i] = uint8_t(i);
    Copy(base_header, kSec + 0x150, Encrypt(master, base_key));
    Pages(base_header, base_image);
    const auto lzx = StoredLzx(base_image);
    auto block = [](std::span<const uint8_t> chunk) {
      Bytes out(24 + 2 + chunk.size() + 2);
      Put16(out, 24, uint16_t(chunk.size())); Copy(out, 26, chunk); return out;
    };
    if (chained) {
      base_body = block(std::span(lzx).first(lzx.size() / 2));
      const auto next = block(std::span(lzx).subspan(lzx.size() / 2));
      Put32(base_body, 0, uint32_t(next.size())); Copy(base_body, 4, Hash(next));
      SetBlock(base_header, base_body);
      base_body.insert(base_body.end(), next.begin(), next.end());
    } else {
      base_body = block(lzx); SetBlock(base_header, base_body);
    }

    target_image = base_image;
    std::fill(target_image.begin() + 16, target_image.begin() + 24, 0);
    std::copy_n(target_image.begin() + 100, 16, target_image.begin() + 200);
    for (size_t i = 0; i < 32; ++i) target_image[300 + i] = uint8_t(0xC0 + i);
    target_header = base_header;
    Copy(target_header, kSec + 0x150, Encrypt(master, target_key));
    Pages(target_header, target_image);
    records = Delta(0, 16, 8, 0);
    const auto copy = Delta(100, 200, 16, 1);
    records.insert(records.end(), copy.begin(), copy.end());
    const auto data = StoredLzx(std::span(target_image).subspan(300, 32));
    const auto compressed = Delta(300, 300, 32, uint16_t(data.size()), data);
    records.insert(records.end(), compressed.begin(), compressed.end());

    patch_header = base_header;
    Put32(patch_header, 4, 0x40); Put32(patch_header, 0x14, 2);
    Put32(patch_header, 0x20, 0x5FF); Put32(patch_header, 0x24, kDelta);
    Put16(patch_header, kFfi + 6, 3);
    Copy(patch_header, kSec + 0x150, Encrypt(target_key, patch_key));
    Put32(patch_header, kDelta + 4, 0x20000200);
    Put32(patch_header, kDelta + 8, 0x20000100);
    Copy(patch_header, kDelta + 0xC, Hash(std::span(base_header).subspan(kSec + 8, 0x100)));
    Copy(patch_header, kDelta + 0x20, Encrypt(target_key, base_key));
    Put32(patch_header, kDelta + 0x30, kHeader);
    Put32(patch_header, kDelta + 0x44, uint32_t(base_image.size()));
    const auto header_data = StoredLzx(target_header);
    const auto header_record = Delta(0, 0, kHeader, uint16_t(header_data.size()), header_data);
    // Give the patch its own larger header to hold the complete compressed record.
    patch_header.resize(0x2000); Put32(patch_header, 8, uint32_t(patch_header.size()));
    Copy(patch_header, kDelta + 0x4C, header_record);
    Put32(patch_header, kDelta, uint32_t(0x4C + header_record.size()));
    std::ofstream(dir / "keys.txt") << "xex2-devkit 00000000000000000000000000000000\n";
  }
  ~Fixture() { std::error_code ec; fs::remove_all(dir, ec); }
  static void Pages(Bytes& header, const Bytes& image) {
    Put32(header, kSec + 0x180, 2);
    Put32(header, kSec + 0x184, 0x11); Put32(header, kSec + 0x19C, 0x12);
    Bytes last(image.begin() + 0x1000, image.end());
    last.insert(last.end(), header.begin() + kSec + 0x19C, header.begin() + kSec + 0x1B4);
    Copy(header, kSec + 0x188, Hash(last));
    Bytes first(image.begin(), image.begin() + 0x1000);
    first.insert(first.end(), header.begin() + kSec + 0x184, header.begin() + kSec + 0x19C);
    Copy(header, kSec + 0x114, Hash(first));
  }
  static void SetBlock(Bytes& header, const Bytes& body) {
    Put32(header, kFfi + 0xC, uint32_t(body.size()));
    Copy(header, kFfi + 0x10, Hash(body));
  }
  x360n::ProcessResult Run(uint16_t encryption = 1) {
    Bytes base = base_header, body = base_body;
    body.resize((body.size() + 15) & ~size_t(15));
    body = Encrypt(base_key, body); base.insert(base.end(), body.begin(), body.end());
    base.insert(base.end(), base_tail, uint8_t(0x5A));
    Bytes patch_body(24);
    if (chained) {
      REQUIRE(records.size() >= 12);
      Bytes next(24); next.insert(next.end(), records.begin() + 12, records.end());
      patch_body.insert(patch_body.end(), records.begin(), records.begin() + 12);
      Put32(patch_body, 0, uint32_t(next.size())); Copy(patch_body, 4, Hash(next));
      SetBlock(patch_header, patch_body);
      if (corrupt_patch_block) next.back() ^= 1;
      patch_body.insert(patch_body.end(), next.begin(), next.end());
    } else {
      patch_body.insert(patch_body.end(), records.begin(), records.end());
      SetBlock(patch_header, patch_body);
      if (corrupt_patch_block) patch_body.back() ^= 1;
    }
    Put16(patch_header, kFfi + 4, encryption);
    if (encryption == 1) {
      patch_body.resize((patch_body.size() + 15) & ~size_t(15));
      patch_body = Encrypt(patch_key, patch_body);
    }
    Bytes patch = patch_header; patch.insert(patch.end(), patch_body.begin(), patch_body.end());
    Write(dir / "base.xex", base); Write(dir / "patch.xexp", patch);
    x360n::ProcessSpec spec;
    spec.exe = X360N_CONTENT_EXE;
    spec.args = {"apply-patch", (dir / "base.xex").string(), (dir / "patch.xexp").string(),
                 (dir / "out").string(), "--keys", (dir / "keys.txt").string()};
    spec.working_dir = dir; spec.stdout_file = dir / "out.log";
    spec.stderr_file = dir / "err.log"; spec.timeout = std::chrono::seconds(10);
    const auto result = x360n::RunProcess(spec);
    CHECK_FALSE(result.timed_out);
    REQUIRE(Read(dir / "base.xex") == base); REQUIRE(Read(dir / "patch.xexp") == patch);
    return result;
  }
  std::string Err() { const auto b = Read(dir / "err.log"); return {b.begin(), b.end()}; }
};
}

TEST_CASE("apply-patch reconstructs a synthetic image and verifies both page links", "[content][xex-patch]") {
  Fixture f;
  bool encrypted = true;
  SECTION("encrypted patch") {}
  SECTION("plaintext patch") { encrypted = false; }
  const auto result = f.Run(encrypted);
  INFO(f.Err()); REQUIRE(result.launched); REQUIRE(result.exit_code == 0);
  CHECK(Read(f.dir / "out/base.2.0.2.0.header.bin") == f.target_header);
  CHECK(Read(f.dir / "out/base.2.0.2.0.image.bin") == f.target_image);
}

TEST_CASE("apply-patch follows chained blocks and rejects a later bad hash", "[content][xex-patch]") {
  Fixture f(true);
  bool valid = true;
  SECTION("two valid blocks") {}
  SECTION("second block corrupted") { f.corrupt_patch_block = true; valid = false; }
  const auto result = f.Run();
  INFO(f.Err()); REQUIRE(result.launched);
  if (valid) {
    REQUIRE(result.exit_code == 0);
    CHECK(Read(f.dir / "out/base.2.0.2.0.header.bin") == f.target_header);
    CHECK(Read(f.dir / "out/base.2.0.2.0.image.bin") == f.target_image);
  } else {
    CHECK(result.exit_code == 1);
    CHECK(f.Err().find("block 1 hash mismatch") != std::string::npos);
    CHECK_FALSE(fs::exists(f.dir / "out"));
  }
}

TEST_CASE("apply-patch rejects incomplete delta records before the decoder", "[content][xex-patch]") {
  Fixture f;
  SECTION("one trailing byte") { f.records.push_back(0); }
  SECTION("eleven trailing bytes") { f.records.insert(f.records.end(), 11, 0); }
  SECTION("truncated payload") { f.records = Delta(0, 16, 8, 10, Bytes(9)); }
  const auto result = f.Run();
  INFO(f.Err()); REQUIRE(result.launched); CHECK(result.exit_code == 1);
  CHECK(f.Err().find("delta") != std::string::npos);
  CHECK_FALSE(fs::exists(f.dir / "out"));
}

TEST_CASE("apply-patch rejects unsupported encryption and invalid LZX windows", "[content][xex-patch]") {
  Fixture f;
  uint16_t encryption = 1;
  std::string error = "window";
  SECTION("unknown encryption") { encryption = 2; error = "encryption 2"; }
  SECTION("zero window") { Put32(f.patch_header, kFfi + 8, 0); }
  SECTION("non power of two window") { Put32(f.patch_header, kFfi + 8, 0x8001); }
  SECTION("window too large for pinned decoder") { Put32(f.patch_header, kFfi + 8, 0x400000); }
  const auto result = f.Run(encryption);
  INFO(f.Err()); REQUIRE(result.launched); CHECK(result.exit_code == 1);
  CHECK(f.Err().find(error) != std::string::npos);
  CHECK_FALSE(fs::exists(f.dir / "out"));
}

TEST_CASE("apply-patch validates descriptor and delta ranges", "[content][xex-patch]") {
  Fixture f;
  std::string error = "delta";
  SECTION("destination outside image") { f.records = Delta(0, 0xFFFFFFFE, 8, 0); }
  SECTION("copy source outside image") { f.records = Delta(0xFFFFFFFE, 16, 8, 1); }
  SECTION("reference exceeds decoder window") {
    Put32(f.patch_header, kDelta + 0x30, 0x10000);
    Copy(f.patch_header, kDelta + 0x4C, Delta(0, 0, 0x8001, 2, Bytes(2)));
    error = "reference exceeds LZX window";
  }
  SECTION("descriptor too small") { Put32(f.patch_header, kDelta, 0x10); error = "descriptor"; }
  SECTION("descriptor extends beyond header") {
    Put32(f.patch_header, kDelta, 0xFFFFFFFF); error = "header";
  }
  SECTION("header delta exceeds descriptor") { Put32(f.patch_header, kDelta, 0x58); }
  SECTION("header move reads newly grown buffer") {
    Put32(f.patch_header, kDelta + 0x30, kHeader * 2);
    Put32(f.patch_header, kDelta + 0x34, kHeader + 16);
    Put32(f.patch_header, kDelta + 0x38, 16); error = "header move";
  }
  SECTION("header move targets discarded tail") {
    Put32(f.patch_header, kDelta + 0x30, kHeader / 2);
    Put32(f.patch_header, kDelta + 0x34, 16);
    Put32(f.patch_header, kDelta + 0x38, 16);
    Put32(f.patch_header, kDelta + 0x3C, kHeader - 16); error = "header move";
  }
  const auto result = f.Run();
  INFO(f.Err()); REQUIRE(result.launched); CHECK(result.exit_code == 1);
  CHECK(f.Err().find(error) != std::string::npos);
  CHECK_FALSE(fs::exists(f.dir / "out"));
}

TEST_CASE("apply-patch handles zero and copy header records without payload bytes", "[content][xex-patch]") {
  Fixture f;
  f.target_header = f.base_header; f.target_image = f.base_image;
  f.records = Bytes(12);
  Copy(f.patch_header, kDelta + 0x20, Encrypt(f.base_key, f.base_key));
  Copy(f.patch_header, kSec + 0x150, Encrypt(f.base_key, f.patch_key));
  Put32(f.patch_header, kDelta, 0x58);
  SECTION("terminator") { Copy(f.patch_header, kDelta + 0x4C, Bytes(12)); }
  SECTION("copy in place") { Copy(f.patch_header, kDelta + 0x4C, Delta(0, 0, kHeader, 1)); }
  const auto result = f.Run();
  INFO(f.Err()); REQUIRE(result.launched); REQUIRE(result.exit_code == 0);
  CHECK(Read(f.dir / "out/base.2.0.2.0.header.bin") == f.target_header);
  CHECK(Read(f.dir / "out/base.2.0.2.0.image.bin") == f.target_image);
}

TEST_CASE("apply-patch preserves existing output files", "[content][xex-patch]") {
  Fixture f;
  fs::create_directories(f.dir / "out");
  const Bytes sentinel{1, 2, 3, 4};
  const auto path = f.dir / "out/base.2.0.2.0.header.bin";
  Write(path, sentinel);
  const auto result = f.Run();
  INFO(f.Err()); REQUIRE(result.launched); CHECK(result.exit_code == 1);
  CHECK(f.Err().find("refusing to overwrite") != std::string::npos);
  CHECK(Read(path) == sentinel);
  CHECK_FALSE(fs::exists(f.dir / "out/base.2.0.2.0.image.bin"));
}

TEST_CASE("apply-patch rejects mismatched identities and digests", "[content][xex-patch]") {
  Fixture f;
  std::string error;
  SECTION("wrong first block hash") {
    f.base_header[kFfi + 0x10] ^= 1; error = "first block";
  }
  SECTION("wrong base page digest") {
    f.base_header[kSec + 0x114] ^= 1; error = "base image does not match";
  }
  SECTION("wrong source signature digest") {
    f.patch_header[kDelta + 0xC] ^= 1; error = "patch is not for this base";
  }
  SECTION("wrong image key source") {
    f.patch_header[kDelta + 0x20] ^= 1; error = "relates the patched header";
  }
  SECTION("wrong result page digest") {
    f.records = Delta(0, 0, 8, 0); error = "result does not match";
  }
  const auto result = f.Run();
  INFO(f.Err()); REQUIRE(result.launched); CHECK(result.exit_code == 1);
  CHECK(f.Err().find(error) != std::string::npos);
  CHECK_FALSE(fs::exists(f.dir / "out"));
}

TEST_CASE("apply-patch keeps compressed chunks inside their authenticated block", "[content][xex-patch]") {
  Fixture f;
  SECTION("chunk payload crosses block end") { f.base_body.resize(28); Put16(f.base_body, 24, 8); }
  SECTION("chunk length crosses block end") { f.base_body.resize(25); }
  Fixture::SetBlock(f.base_header, f.base_body);
  const auto result = f.Run();
  INFO(f.Err()); REQUIRE(result.launched); CHECK(result.exit_code == 1);
  CHECK(f.Err().find("base: chunk") != std::string::npos);
  CHECK_FALSE(fs::exists(f.dir / "out"));
}

TEST_CASE("apply-patch rejects malformed header ranges before allocation", "[content][xex-patch]") {
  Fixture f;
  SECTION("no page descriptors") { Put32(f.base_header, kSec + 0x180, 0); }
  SECTION("descriptor count outside header") { Put32(f.base_header, kSec + 0x180, 0xFFFFFFFF); }
  SECTION("wrapped security offset") { Put32(f.base_header, 0x10, 0xFFFFFF00); }
  SECTION("header extends beyond file") { Put32(f.base_header, 8, 0xFFFFFFFF); }
  SECTION("optional header count outside header") { Put32(f.base_header, 0x14, 0xFFFFFFFF); }
  SECTION("image page total overflow") {
    Put32(f.base_header, kSec + 0x184, 0xFFFFFFF1);
    Put32(f.base_header, kSec + 0x19C, 0xFFFFFFF2);
  }
  const auto result = f.Run();
  INFO(f.Err()); REQUIRE(result.launched); CHECK(result.exit_code == 1);
  CHECK(f.Err().find("header") != std::string::npos);
  CHECK_FALSE(fs::exists(f.dir / "out"));
}

TEST_CASE("apply-patch accepts layouts the SDK loader accepts", "[content][xex-patch]") {
  Fixture f;
  // aes_decrypt_buffer decrypts whole blocks and leaves a tail unchanged; the
  // block and page digests still authenticate the decoded image.
  SECTION("unaligned bytes after the encrypted body") { f.base_tail = 5; }
  // With a zero source offset the source size moves nothing; a size beyond
  // the base image only means there is no tail to clear.
  SECTION("zero image source offset with a larger size") {
    Put32(f.patch_header, kDelta + 0x44, uint32_t(f.base_image.size() + 0x1000));
  }
  const auto result = f.Run();
  INFO(f.Err()); REQUIRE(result.launched); REQUIRE(result.exit_code == 0);
  CHECK(Read(f.dir / "out/base.2.0.2.0.header.bin") == f.target_header);
  CHECK(Read(f.dir / "out/base.2.0.2.0.image.bin") == f.target_image);
}
