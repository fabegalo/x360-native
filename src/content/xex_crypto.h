#pragma once
// AES-128-CBC (zero IV, whole blocks; a partial tail is copied unchanged) and
// SHA-1 for the XEX formats. Windows uses CNG; other systems the pinned SDK's
// aes_128 and TinySHA1. Both are held to the same known-answer tests.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x360n::content::crypto {
std::vector<uint8_t> AesCbcDecrypt(const uint8_t* key16, const uint8_t* data, size_t len);
std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t len);
}  // namespace x360n::content::crypto
