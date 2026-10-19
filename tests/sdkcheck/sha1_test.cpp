#include <catch2/catch_test_macros.hpp>
#include "../../external/rexglue-sdk/thirdparty/crypto/TinySHA1.hpp"
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

extern "C" REX_FUNC(__imp__XeCryptSha);
extern "C" REX_FUNC(__imp__XeCryptShaInit);
extern "C" REX_FUNC(__imp__XeCryptShaUpdate);
extern "C" REX_FUNC(__imp__XeCryptShaFinal);

namespace {
std::string Digest(sha1::SHA1 hash) {
  uint8_t bytes[20];
  hash.finalize(bytes);
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
  return result;
}
}

TEST_CASE("SHA1 loader implementation matches standard vectors and chunk boundaries", "[sdk][sha1]") {
  for (const auto& [input, expected] : std::array<std::pair<std::string, std::string>, 4>{{
      {"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
      {"abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
      {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
      {std::string(1'000'000, 'a'), "34aa973cd4c4daa4f61eeb2bdbad27316534016f"}}}) {
    INFO("input bytes " << input.size());
    sha1::SHA1 whole;
    whole.processBytes(input.data(), input.size());
    CHECK(Digest(whole) == expected);
    for (size_t chunk : {1u, 55u, 56u, 63u, 64u, 65u, 4096u}) {
      sha1::SHA1 streaming;
      for (size_t offset = 0; offset < input.size(); offset += chunk)
        streaming.processBytes(input.data() + offset, std::min(chunk, input.size() - offset));
      CHECK(Digest(streaming) == expected);
    }
  }
}

TEST_CASE("Guest SHA1 exports preserve big-endian state and bounded digest output", "[sdk][sha1]") {
  using rex::X_STATUS;
  rex::Runtime runtime(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* memory = runtime.memory();
  const auto address = memory->SystemHeapAlloc(512);
  REQUIRE(address);
  auto* base = memory->virtual_membase();
  std::memset(base + address, 0xA5, 512);
  std::memcpy(base + address + 128, "abc", 3);
  const std::array<uint8_t, 20> expected{0xA9,0x99,0x3E,0x36,0x47,0x06,0x81,0x6A,0xBA,0x3E,
                                       0x25,0x71,0x78,0x50,0xC2,0x6C,0x9C,0xD0,0xD8,0x9D};
  PPCContext ctx{};
  ctx.r3.u64 = address;
  __imp__XeCryptShaInit(ctx, base);
  CHECK(rex::memory::load_and_swap<uint32_t>(base + address) == 0);
  CHECK(rex::memory::load_and_swap<uint32_t>(base + address + 4) == 0x67452301);
  CHECK(base[address + 0x58] == 0xA5);
  for (uint32_t offset = 0; offset < 3; ++offset) {
    ctx.r3.u64 = address; ctx.r4.u64 = address + 128 + offset; ctx.r5.u64 = 1;
    __imp__XeCryptShaUpdate(ctx, base);
    CHECK(rex::memory::load_and_swap<uint32_t>(base + address) == offset + 1);
  }
  ctx.r3.u64 = address; ctx.r4.u64 = address + 256; ctx.r5.u64 = 40;
  __imp__XeCryptShaFinal(ctx, base);
  CHECK(std::equal(expected.begin(), expected.end(), base + address + 256));
  CHECK(base[address + 276] == 0xA5);
  CHECK(base[address + 0x58] == 0xA5);
  // Three separate ABI inputs hash their concatenation, with truncated output.
  ctx.r3.u64 = address + 128; ctx.r4.u64 = 1;
  ctx.r5.u64 = address + 129; ctx.r6.u64 = 1;
  ctx.r7.u64 = address + 130; ctx.r8.u64 = 1;
  ctx.r9.u64 = address + 300; ctx.r10.u64 = 7;
  __imp__XeCryptSha(ctx, base);
  CHECK(std::equal(expected.begin(), expected.begin() + 7, base + address + 300));
  CHECK(base[address + 307] == 0xA5);
  memory->SystemHeapFree(address);
}

#ifdef _WIN32
namespace {
struct WindowsSha1 {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  WindowsSha1() {
    REQUIRE(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0);
    REQUIRE(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0);
  }
  ~WindowsSha1() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
  void Add(const uint8_t* bytes, size_t size) {
    REQUIRE(BCryptHashData(hash, const_cast<PUCHAR>(bytes), ULONG(size), 0) == 0);
  }
  std::string Finish() {
    uint8_t bytes[20];
    REQUIRE(BCryptFinishHash(hash, bytes, sizeof(bytes), 0) == 0);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
  }
};
}
TEST_CASE("SHA1 binary block digests match Windows cryptography", "[sdk][sha1]") {
  std::vector<uint8_t> bytes(65536);
  uint32_t random = 0x12345678;
  for (auto& byte : bytes) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    byte = uint8_t(random);
  }
  for (size_t size : {0u, 1u, 55u, 56u, 57u, 63u, 64u, 65u, 127u, 128u, 129u, 55296u, 65536u}) {
    INFO("block bytes " << size);
    WindowsSha1 reference;
    reference.Add(bytes.data(), size);
    const auto expected = reference.Finish();
    sha1::SHA1 actual;
    actual.processBytes(bytes.data(), size);
    CHECK(Digest(actual) == expected);
    // The loader resets this same object for each XEX compressed block.
    actual.reset().processBytes(bytes.data(), size);
    CHECK(Digest(actual) == expected);
  }
}
TEST_CASE("SHA1 retains the high bits of the padded message length", "[sdk][sha1]") {
  // At 512 MiB the bit count needs a nonzero high word. Stream a fixed buffer;
  // this test uses 1 MiB of input storage and an independent Windows oracle.
  std::vector<uint8_t> block(1024 * 1024, 'a');
  sha1::SHA1 actual;
  WindowsSha1 reference;
  for (unsigned i = 0; i < 512; ++i) {
    actual.processBytes(block.data(), block.size());
    reference.Add(block.data(), block.size());
  }
  CHECK(Digest(actual) == reference.Finish());
}
#endif
