#pragma once
#include <cstdint>
#include <bit>
#include <rex/graphics/xenos.h>
namespace x360n::native {
// Guest RGBA4 stores R in the least significant nibble. Replication is an
// exact conversion to UNORM8 (n / 15 == (n * 17) / 255), before fetch swizzle.
constexpr uint32_t ExpandRgba4(uint16_t value) {
  return ((value & 15u) * 17u) | (((value >> 4) & 15u) * 17u << 8) |
      (((value >> 8) & 15u) * 17u << 16) | (((value >> 12) & 15u) * 17u << 24);
}
constexpr float Rgba4Snorm(unsigned nibble) {
  const int value = nibble < 8 ? int(nibble) : int(nibble) - 16;
  return value == -8 ? -1.0f : float(value) / 7.0f;
}
constexpr float PackedSnorm(unsigned value, unsigned bits) {
  const unsigned sign = 1u << (bits - 1);
  const int signed_value = value & sign ? int(value) - int(sign * 2) : int(value);
  return signed_value == -int(sign) ? -1.0f : float(signed_value) / float(sign - 1);
}
// 24 depth bits followed by eight stencil bits. Xenos 20e4 is unsigned,
// bias 15, with denormals (no infinity encoding); preserve exact f32 bits.
inline float DecodeDepthTexture(uint32_t packed, bool floating) {
  uint32_t depth = packed >> 8;
  // As the pinned texture_load_depth_unorm shader: (d + d/2^23) / 2^24, exact
  // at 0 and at 0xFFFFFF (1.0).
  if (!floating) return float(depth + (depth >> 23)) * 0x1p-24f;
  if (!depth) return 0.f;
  uint32_t mantissa = depth & 0xFFFFF, exponent = depth >> 20;
  if (!exponent) {
    uint32_t shift = std::countl_zero(mantissa) - 11;
    exponent = 1u - shift;
    mantissa = (mantissa << shift) & 0xFFFFF;
  }
  return std::bit_cast<float>(((exponent + 112) << 23) | (mantissa << 3));
}
// frac/int only selects the representation of fixed-point formats. IEEE
// floating texture encodings keep their exponent/mantissa in either mode.
constexpr bool IsFloatTexture(rex::graphics::xenos::TextureFormat f) {
  using F = rex::graphics::xenos::TextureFormat;
  return f == F::k_16_FLOAT || f == F::k_16_16_FLOAT || f == F::k_16_16_16_16_FLOAT ||
      f == F::k_32_FLOAT || f == F::k_32_32_FLOAT || f == F::k_32_32_32_32_FLOAT;
}
// Endian swap the pinned texture_load_* shaders apply per block size: none
// for 8-bit blocks, only 8in16 for 16-bit blocks, every mode from 32 bits.
constexpr rex::graphics::xenos::Endian TextureBlockEndian(unsigned block_bytes, rex::graphics::xenos::Endian endian) {
  using E = rex::graphics::xenos::Endian;
  return block_bytes == 1 ? E::kNone : block_bytes == 2 && endian != E::k8in16 ? E::kNone : endian;
}
// API-independent description; this is also the boundary for a future Vulkan backend.
struct ScanoutTexture {
  uint32_t address, width, height, pitch, tiled, endian, swizzle, bytes;
  bool rgb10 = false;
};
ScanoutTexture DecodeScanout(const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch,
                            uint32_t active_width, uint32_t active_height);
}
