// The extractor's AES-128-CBC and SHA-1 (CNG on Windows, the pinned SDK's
// aes_128/TinySHA1 elsewhere) against the same known answers on every system.
#include <catch2/catch_test_macros.hpp>
#include <string>
#include "../../src/content/xex_crypto.h"

using namespace x360n::content::crypto;
namespace {
std::vector<uint8_t> FromHex(const std::string& hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return out;
}
std::vector<uint8_t> Bytes(const std::array<uint8_t, 20>& d) { return {d.begin(), d.end()}; }
}  // namespace
TEST_CASE("XEX AES-128-CBC and SHA-1 match known answers", "[xex-crypto]") {
  // FIPS-197 appendix C.1: AES-128 of 00112233... under key 00010203...
  const auto key = FromHex("000102030405060708090a0b0c0d0e0f");
  const auto plain = FromHex("00112233445566778899aabbccddeeff");
  const auto cipher = FromHex("69c4e0d86a7b0430d8cdb78070b4c55a");
  // Zero IV: block 1 decrypts to P, block 2 (same ciphertext) to P xor C; a tail stays as is.
  std::vector<uint8_t> input = cipher;
  input.insert(input.end(), cipher.begin(), cipher.end());
  input.insert(input.end(), {0xAA, 0xBB, 0xCC});
  std::vector<uint8_t> expected = plain;
  for (int i = 0; i < 16; ++i) expected.push_back(uint8_t(plain[size_t(i)] ^ cipher[size_t(i)]));
  expected.insert(expected.end(), {0xAA, 0xBB, 0xCC});
  CHECK(AesCbcDecrypt(key.data(), input.data(), input.size()) == expected);
  CHECK(AesCbcDecrypt(key.data(), input.data(), 3) == std::vector<uint8_t>(input.begin(), input.begin() + 3));
  // FIPS 180 examples: "abc" and one million 'a'.
  const std::string abc = "abc";
  CHECK(Bytes(Sha1(reinterpret_cast<const uint8_t*>(abc.data()), abc.size())) == FromHex("a9993e364706816aba3e25717850c26c9cd0d89d"));
  const std::vector<uint8_t> million(1000000, 'a');
  CHECK(Bytes(Sha1(million.data(), million.size())) == FromHex("34aa973cd4c4daa4f61eeb2bdbad27316534016f"));
}
