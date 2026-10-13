#include "sha256.h"

#include <fstream>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace x360n {
namespace {

constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

}  // namespace

Sha256::Sha256()
    : h_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::Block(const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
           (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
  uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + ch + kK[i] + w[i];
    uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
  h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::Update(std::span<const uint8_t> data) {
  total_len_ += data.size();
  size_t i = 0;
  if (buf_len_) {
    while (buf_len_ < 64 && i < data.size()) buf_[buf_len_++] = data[i++];
    if (buf_len_ < 64) return;
    Block(buf_.data());
    buf_len_ = 0;
  }
  for (; i + 64 <= data.size(); i += 64) Block(data.data() + i);
  while (i < data.size()) buf_[buf_len_++] = data[i++];
}

std::array<uint8_t, 32> Sha256::Finish() {
  uint64_t bit_len = total_len_ * 8;
  uint8_t pad = 0x80;
  Update({&pad, 1});
  uint8_t zero = 0;
  while (buf_len_ != 56) Update({&zero, 1});
  uint8_t len_be[8];
  for (int i = 0; i < 8; ++i) len_be[i] = uint8_t(bit_len >> (56 - 8 * i));
  Update({len_be, 8});
  std::array<uint8_t, 32> out;
  for (int i = 0; i < 8; ++i) {
    out[i * 4] = uint8_t(h_[i] >> 24);
    out[i * 4 + 1] = uint8_t(h_[i] >> 16);
    out[i * 4 + 2] = uint8_t(h_[i] >> 8);
    out[i * 4 + 3] = uint8_t(h_[i]);
  }
  return out;
}

std::string ToHex(std::span<const uint8_t> bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string s;
  s.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) {
    s.push_back(kDigits[b >> 4]);
    s.push_back(kDigits[b & 0xF]);
  }
  return s;
}

std::string Sha256Hex(std::span<const uint8_t> data) {
  Sha256 h;
  h.Update(data);
  auto d = h.Finish();
  return ToHex(d);
}

std::optional<FileDigest> DigestFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
#ifdef _WIN32
  // Preparation checks stream the compiler and full build dependency closure.
  // Use Windows' SHA-256 implementation without a timestamp/hash cache, retaining
  // the same digest contract and the portable implementation on other hosts.
  struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    Algorithm() { if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) handle = nullptr; }
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
  };
  static const Algorithm algorithm;
  struct Hash {
    BCRYPT_HASH_HANDLE handle = nullptr;
    ~Hash() { if (handle) BCryptDestroyHash(handle); }
  } hash;
  if (!algorithm.handle || BCryptCreateHash(algorithm.handle, &hash.handle, nullptr, 0, nullptr, 0, 0) < 0)
    return std::nullopt;
#else
  Sha256 h;
#endif
  std::vector<uint8_t> buf(1 << 20);
  uint64_t size = 0;
  while (in) {
    in.read(reinterpret_cast<char*>(buf.data()), std::streamsize(buf.size()));
    auto n = size_t(in.gcount());
    if (n == 0) break;
#ifdef _WIN32
    if (BCryptHashData(hash.handle, buf.data(), static_cast<ULONG>(n), 0) < 0) return std::nullopt;
#else
    h.Update({buf.data(), n});
#endif
    size += n;
  }
  if (in.bad()) return std::nullopt;
#ifdef _WIN32
  std::array<uint8_t, 32> d{};
  if (BCryptFinishHash(hash.handle, d.data(), static_cast<ULONG>(d.size()), 0) < 0) return std::nullopt;
#else
  auto d = h.Finish();
#endif
  return FileDigest{size, ToHex(d)};
}

}  // namespace x360n
