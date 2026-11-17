// AES-128-CBC and SHA-1 for the XEX formats (see xex_crypto.h).
#include "xex_crypto.h"
#if defined(_WIN32) && !defined(X360N_CONTENT_TEST_EXCRYPT)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#if !defined(_WIN32) || defined(X360N_CONTENT_TEST_EXCRYPT)
#include <excrypt.h>
#endif

namespace x360n::content {
namespace crypto {
#if !defined(_WIN32) || defined(X360N_CONTENT_TEST_EXCRYPT)
std::vector<uint8_t> AesCbcDecrypt(const uint8_t* key16, const uint8_t* data, size_t len) {
  std::vector<uint8_t> out(data, data + len);
  // ExCrypt's AES-NI key schedule uses aligned SIMD loads and stores.
  alignas(16) EXCRYPT_AES_STATE state;
  ExCryptAesKey(&state, key16);
  uint8_t previous[16] = {};
  for (size_t at = 0; at + 16 <= len; at += 16) {
    uint8_t plain[16];
    ExCryptAesEcb(&state, data + at, plain, 0);
    for (int i = 0; i < 16; ++i) out[at + i] = uint8_t(plain[i] ^ previous[i]);
    std::memcpy(previous, data + at, 16);
  }
  return out;
}
std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t len) {
  EXCRYPT_SHA_STATE hash;
  ExCryptShaInit(&hash);
  while (len) {
    const auto chunk = uint32_t(std::min(len, size_t(std::numeric_limits<uint32_t>::max())));
    ExCryptShaUpdate(&hash, data, chunk);
    data += chunk;
    len -= chunk;
  }
  std::array<uint8_t, 20> digest{};
  ExCryptShaFinal(&hash, digest.data(), uint32_t(digest.size()));
  return digest;
}
#else
std::vector<uint8_t> AesCbcDecrypt(const uint8_t* key16, const uint8_t* data, size_t len) {
  if (len > std::numeric_limits<ULONG>::max()) throw std::runtime_error("AES ciphertext size is not supported");
  std::vector<uint8_t> out(data, data + len);
  const ULONG whole = ULONG(len & ~size_t(15));
  if (!whole) return out;
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_KEY_HANDLE k = nullptr;
  bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0 &&
            BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                              sizeof(BCRYPT_CHAIN_MODE_CBC), 0) == 0 &&
            BCryptGenerateSymmetricKey(alg, &k, nullptr, 0, (PUCHAR)key16, 16, 0) == 0;
  uint8_t iv[16] = {};
  ULONG got = 0;
  ok = ok && BCryptDecrypt(k, (PUCHAR)data, whole, nullptr, iv, 16, out.data(), whole, &got, 0) == 0 &&
       got == whole;
  if (k) BCryptDestroyKey(k);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  if (!ok) throw std::runtime_error("AES decryption failed (CNG)");
  return out;
}
std::array<uint8_t, 20> Sha1(const uint8_t* p, size_t n) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::array<uint8_t, 20> d{};
  bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0 &&
            BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
  while (ok && n) {
    const auto chunk = ULONG(std::min(n, size_t(std::numeric_limits<ULONG>::max())));
    ok = BCryptHashData(hash, const_cast<PUCHAR>(p), chunk, 0) == 0;
    p += chunk;
    n -= chunk;
  }
  ok = ok && BCryptFinishHash(hash, d.data(), ULONG(d.size()), 0) == 0;
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  if (!ok) throw std::runtime_error("SHA-1 failed (CNG)");
  return d;
}
#endif
}  // namespace crypto
}  // namespace x360n::content
