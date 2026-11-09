#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <vector>
#include <fmt/format.h>
#include <rex/cvar.h>
#include "../../external/rexglue-sdk/thirdparty/crypto/TinySHA1.hpp"
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace rex::kernel::xboxkrnl {
bool LoadRsaPublicKey(uint32_t index, std::array<uint8_t, 0x110>& key);
bool VerifyRsaSignature(const uint8_t* key, const uint8_t* digest, const uint8_t* signature);
bool LoadLocalKey(const std::string& wanted, std::array<uint8_t, 0x110>& key, bool quiet);
std::array<uint8_t, 0x100> BeSigFormat(const uint8_t* hash, const uint8_t* salt);
bool VerifyBeSignature(const uint8_t* key, const uint8_t* hash, const uint8_t* salt, const uint8_t* signature);
void RotSumSha(const uint8_t* in1, size_t size1, const uint8_t* in2, size_t size2, uint8_t* digest,
               size_t digest_size);
bool IsOriginalSignature(std::initializer_list<std::pair<const void*, size_t>> parts);
}  // namespace rex::kernel::xboxkrnl

namespace {
namespace fs = std::filesystem;

void WriteKeys(const fs::path& path, const std::string& text) {
  std::ofstream(path) << text;
  rex::cvar::SetFlagByName("xekeys_rsa_public_file", path.string());
}
}  // namespace

#ifdef _WIN32
TEST_CASE("Kernel RSA signatures are verified against a local public key", "[sdk][xekeys-rsa]") {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_KEY_HANDLE handle = nullptr;
  REQUIRE(BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_RSA_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0)));
  REQUIRE(BCRYPT_SUCCESS(BCryptGenerateKeyPair(alg, &handle, 2048, 0)));
  REQUIRE(BCRYPT_SUCCESS(BCryptFinalizeKeyPair(handle, 0)));
  std::vector<uint8_t> pub(1024);
  ULONG size = 0;
  REQUIRE(BCRYPT_SUCCESS(BCryptExportKey(handle, nullptr, BCRYPT_RSAPUBLIC_BLOB, pub.data(), ULONG(pub.size()), &size, 0)));
  const auto* header = reinterpret_cast<const BCRYPT_RSAKEY_BLOB*>(pub.data());
  REQUIRE(header->cbModulus == 0x100);
  REQUIRE(header->cbPublicExp <= 4);
  // XECRYPT_RSAPUB_2048: be32 0x20, be32 exponent, reserved, modulus qwords least significant first.
  std::array<uint8_t, 0x110> key{};
  key[3] = 0x20;
  std::memcpy(&key[8 - header->cbPublicExp], &pub[sizeof(*header)], header->cbPublicExp);
  const uint8_t* modulus = &pub[sizeof(*header) + header->cbPublicExp];
  for (size_t i = 0; i < 32; ++i) std::memcpy(&key[0x10 + i * 8], modulus + (31 - i) * 8, 8);

  // PKCS#1 v1.5 SHA-1 block, as decoded from the original 17559 system.manifest.
  uint8_t digest[20];
  for (int i = 0; i < 20; ++i) digest[i] = uint8_t(0x30 + i);
  const uint8_t info[] = {0x00, 0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x05, 0x00, 0x04, 0x14};
  std::array<uint8_t, 0x100> block{}, be{}, signature{};
  block[1] = 1;
  std::fill(block.begin() + 2, block.end() - 36, uint8_t(0xFF));
  std::memcpy(&block[0x100 - 36], info, sizeof(info));
  std::memcpy(&block[0x100 - 20], digest, 20);
  ULONG written = 0;
  REQUIRE(BCRYPT_SUCCESS(BCryptDecrypt(handle, block.data(), 0x100, nullptr, nullptr, 0, be.data(), 0x100, &written,
                                       BCRYPT_PAD_NONE)));
  BCryptDestroyKey(handle);
  BCryptCloseAlgorithmProvider(alg, 0);
  std::reverse_copy(be.begin(), be.end(), signature.begin());  // Stored least significant byte first.

  using rex::kernel::xboxkrnl::VerifyRsaSignature;
  CHECK(VerifyRsaSignature(key.data(), digest, signature.data()));
  CHECK_FALSE(VerifyRsaSignature(key.data(), digest, be.data()));  // The big-endian form is not accepted.
  uint8_t other[20];
  std::memcpy(other, digest, 20);
  other[19] ^= 1;
  CHECK_FALSE(VerifyRsaSignature(key.data(), other, signature.data()));
  signature[0x80] ^= 1;
  CHECK_FALSE(VerifyRsaSignature(key.data(), digest, signature.data()));

  std::string hex;
  for (auto b : key) hex += fmt::format("{:02x}", b);
  std::array<uint8_t, 0x110> loaded{};
  const auto file = fs::temp_directory_path() / "x360n-rsa-public-keys.txt";
  WriteKeys(file, "# local\nrsa-pub-0 " + hex + "\n");
  CHECK(rex::kernel::xboxkrnl::LoadRsaPublicKey(0, loaded));
  CHECK(loaded == key);
  CHECK_FALSE(rex::kernel::xboxkrnl::LoadRsaPublicKey(1, loaded));  // Never another key.
  WriteKeys(file, "rsa-pub-0 " + hex + "\nrsa-pub-0 " + hex + "\n");
  CHECK_FALSE(rex::kernel::xboxkrnl::LoadRsaPublicKey(0, loaded));  // Duplicate names are rejected.
  WriteKeys(file, "rsa-pub-0 00\n");
  CHECK_FALSE(rex::kernel::xboxkrnl::LoadRsaPublicKey(0, loaded));
  fs::remove(file);
  rex::cvar::SetFlagByName("xekeys_rsa_public_file", (fs::temp_directory_path() / "x360n-missing-rsa.txt").string());
  CHECK_FALSE(rex::kernel::xboxkrnl::LoadRsaPublicKey(0, loaded));

  // XeCryptBnQwBeSigVerify: block shape here; the positive case is the original
  // XTT below (Montgomery-form result, so a raw signature of the block is refused).
  const uint8_t salt[10] = {'X', 'B', 'O', 'X', '3', '6', '0', 'X', 'T', 'T'};
  const auto formatted = rex::kernel::xboxkrnl::BeSigFormat(digest, salt);
  CHECK(formatted[0xFF] == 0xBC);
  CHECK(formatted[0] < 0x80);
  CHECK_FALSE(rex::kernel::xboxkrnl::VerifyBeSignature(key.data(), digest, salt, signature.data()));
  std::array<uint8_t, 0x110> not_rsa = key;
  not_rsa[3] = 0x10;
  CHECK_FALSE(rex::kernel::xboxkrnl::VerifyBeSignature(not_rsa.data(), digest, salt, signature.data()));
}

// Fidelity: XAM 17559 81795B68 hashes an XTT body with XeCryptRotSumSha; the
// result must equal the record's type hash in the original system.manifest.
TEST_CASE("XeCryptRotSumSha matches the original 17559 XenonSCLatin.xtt digest", "[sdk][xekeys-rsa][local]") {
  const auto xtt = fs::path(X360N_SOURCE_ROOT) / ".local/inputs/dashboards/17559/runtime/XenonSCLatin.xtt";
  if (!fs::exists(xtt)) SKIP("needs local " + xtt.string());
  std::ifstream in(xtt, std::ios::binary);
  const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
  REQUIRE(bytes.size() > 280);
  REQUIRE(std::memcmp(bytes.data(), "xttf", 4) == 0);
  const uint32_t length = uint32_t(bytes[260]) << 24 | bytes[261] << 16 | bytes[262] << 8 | bytes[263];
  REQUIRE(length <= bytes.size() - 260);
  uint8_t digest[20];
  rex::kernel::xboxkrnl::RotSumSha(bytes.data() + 260, length, nullptr, 0, digest, sizeof(digest));
  const uint8_t expected[20] = {0x4a, 0x85, 0x8c, 0x94, 0x75, 0xad, 0x43, 0x4c, 0x67, 0x13,
                                0xf3, 0xef, 0x08, 0x32, 0xfa, 0xcb, 0x6f, 0xc8, 0x6a, 0xe1};
  CHECK(std::memcmp(digest, expected, 20) == 0);

  // The signature at +4 needs the user's XeKeysGetKey(0x39) public key.
  const auto keys = fs::path(X360N_SOURCE_ROOT) / ".local/keys/rsa-public-keys.txt";
  rex::cvar::SetFlagByName("xekeys_rsa_public_file", keys.string());
  std::array<uint8_t, 0x110> key{};
  if (!fs::exists(keys) || !rex::kernel::xboxkrnl::LoadLocalKey("xekey-0x39", key, true))
    SKIP("needs xekey-0x39 in " + keys.string());
  const uint8_t salt[10] = {'X', 'B', 'O', 'X', '3', '6', '0', 'X', 'T', 'T'};
  using rex::kernel::xboxkrnl::VerifyBeSignature;
  CHECK(VerifyBeSignature(key.data(), digest, salt, bytes.data() + 4));
  digest[0] ^= 1;
  CHECK_FALSE(VerifyBeSignature(key.data(), digest, salt, bytes.data() + 4));
  digest[0] ^= 1;
  uint8_t other_salt[10];
  std::memcpy(other_salt, salt, 10);
  other_salt[9] ^= 1;
  CHECK_FALSE(VerifyBeSignature(key.data(), digest, other_salt, bytes.data() + 4));
}

// Fidelity: XAM 17559 816EBE40 hashes system.manifest from 312 for the be32 length
// at 316, compares the stored digest at 36 and passes key 0 with the signature at 56.
TEST_CASE("The original 17559 system.manifest verifies with the user's kernel key 0", "[sdk][xekeys-rsa][local]") {
  const fs::path root = X360N_SOURCE_ROOT;
  const auto keys = root / ".local/keys/rsa-public-keys.txt";
  const auto manifest = root / ".local/inputs/dashboards/17559/runtime/systemext/system.manifest";
  if (!fs::exists(keys) || !fs::exists(manifest)) SKIP("needs local " + keys.string() + " and " + manifest.string());
  std::ifstream in(manifest, std::ios::binary);
  const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
  REQUIRE(bytes.size() > 320);
  REQUIRE(std::memcmp(bytes.data(), "XMNP", 4) == 0);
  const uint32_t length = uint32_t(bytes[316]) << 24 | bytes[317] << 16 | bytes[318] << 8 | bytes[319];
  REQUIRE(length <= bytes.size() - 312);
  uint8_t digest[20];
  sha1::SHA1 sha;
  sha.processBytes(bytes.data() + 312, length);
  sha.finalize(digest);
  REQUIRE(std::memcmp(digest, bytes.data() + 36, 20) == 0);
  rex::cvar::SetFlagByName("xekeys_rsa_public_file", keys.string());
  std::array<uint8_t, 0x110> key{};
  REQUIRE(rex::kernel::xboxkrnl::LoadRsaPublicKey(0, key));
  CHECK(rex::kernel::xboxkrnl::VerifyRsaSignature(key.data(), digest, bytes.data() + 56));
  digest[0] ^= 1;
  CHECK_FALSE(rex::kernel::xboxkrnl::VerifyRsaSignature(key.data(), digest, bytes.data() + 56));
}

// The release verifies the original files without the user's keys: each pinned
// signature must be one the real key accepts, and nothing else may match a pin.
TEST_CASE("Pinned original signatures are exactly the ones the user's keys verify", "[sdk][xekeys-rsa][local]") {
  const fs::path root = X360N_SOURCE_ROOT;
  const auto runtime = root / ".local/inputs/dashboards/17559/runtime";
  const auto keys = root / ".local/keys/rsa-public-keys.txt";
  const uint8_t junk[0x100] = {};
  CHECK_FALSE(rex::kernel::xboxkrnl::IsOriginalSignature({{junk, 4}, {junk, 20}, {junk, 0x100}}));
  if (!fs::exists(runtime / "systemext/system.manifest")) SKIP("needs local " + runtime.string());
  auto read = [](const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(in), {}};
  };
  const bool have_keys = fs::exists(keys);
  rex::cvar::SetFlagByName("xekeys_rsa_public_file", keys.string());
  const auto manifest = read(runtime / "systemext/system.manifest");
  REQUIRE(manifest.size() > 320);
  const uint32_t length = uint32_t(manifest[316]) << 24 | manifest[317] << 16 | manifest[318] << 8 | manifest[319];
  REQUIRE(length <= manifest.size() - 312);
  uint8_t digest[20];
  sha1::SHA1 sha;
  sha.processBytes(manifest.data() + 312, length);
  sha.finalize(digest);
  const uint8_t key0[4] = {};
  CHECK(rex::kernel::xboxkrnl::IsOriginalSignature({{key0, 4}, {digest, 20}, {manifest.data() + 56, 0x100}}));
  const uint8_t key1[4] = {0, 0, 0, 1};
  CHECK_FALSE(rex::kernel::xboxkrnl::IsOriginalSignature({{key1, 4}, {digest, 20}, {manifest.data() + 56, 0x100}}));
  std::array<uint8_t, 0x110> key{};
  if (have_keys && rex::kernel::xboxkrnl::LoadRsaPublicKey(0, key))
    CHECK(rex::kernel::xboxkrnl::VerifyRsaSignature(key.data(), digest, manifest.data() + 56));
  digest[0] ^= 1;
  CHECK_FALSE(rex::kernel::xboxkrnl::IsOriginalSignature({{key0, 4}, {digest, 20}, {manifest.data() + 56, 0x100}}));
  // System PIRS packages (XAM 8167C370): key 0 over SHA-1 of header 0x22C..0x344.
  const bool have_key0 = have_keys && rex::kernel::xboxkrnl::LoadRsaPublicKey(0, key);
  for (const char* index : {"1", "2", "6", "8"}) {
    INFO(index);
    std::vector<uint8_t> package(0x344);
    std::ifstream(runtime / "systemext/Content/0000000000000000/FFFE07DF/00008000" /
                      (std::string("FFFE07DF0000000") + index), std::ios::binary)
        .read(reinterpret_cast<char*>(package.data()), package.size());
    REQUIRE(std::equal(package.begin(), package.begin() + 4, "PIRS"));
    sha1::SHA1 header;
    header.processBytes(package.data() + 0x22C, 0x344 - 0x22C);
    header.finalize(digest);
    CHECK(rex::kernel::xboxkrnl::IsOriginalSignature({{key0, 4}, {digest, 20}, {package.data() + 4, 0x100}}));
    if (have_key0) CHECK(rex::kernel::xboxkrnl::VerifyRsaSignature(key.data(), digest, package.data() + 4));
    digest[0] ^= 1;
    CHECK_FALSE(rex::kernel::xboxkrnl::IsOriginalSignature({{key0, 4}, {digest, 20}, {package.data() + 4, 0x100}}));
  }
  const bool have_xtt_key = have_keys && rex::kernel::xboxkrnl::LoadLocalKey("xekey-0x39", key, true);
  const uint8_t salt[10] = {'X', 'B', 'O', 'X', '3', '6', '0', 'X', 'T', 'T'};
  for (const char* name : {"XenonSCLatin.xtt", "xenonclatin.xtt", "xenonjklatin.xtt", "SegoeXbox-Light.xtt"}) {
    INFO(name);
    const auto font = read(runtime / name);
    REQUIRE(font.size() > 280);
    const uint32_t size = uint32_t(font[260]) << 24 | font[261] << 16 | font[262] << 8 | font[263];
    REQUIRE(size <= font.size() - 260);
    rex::kernel::xboxkrnl::RotSumSha(font.data() + 260, size, nullptr, 0, digest, sizeof(digest));
    CHECK(rex::kernel::xboxkrnl::IsOriginalSignature({{digest, 20}, {salt, 10}, {font.data() + 4, 0x100}}));
    if (have_xtt_key) CHECK(rex::kernel::xboxkrnl::VerifyBeSignature(key.data(), digest, salt, font.data() + 4));
    digest[19] ^= 1;
    CHECK_FALSE(rex::kernel::xboxkrnl::IsOriginalSignature({{digest, 20}, {salt, 10}, {font.data() + 4, 0x100}}));
  }
  if (!have_xtt_key) WARN("pins checked without xekey-0x39: real verification skipped");
}
#endif
