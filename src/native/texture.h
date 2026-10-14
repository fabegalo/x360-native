#pragma once
#include <cstdint>
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
// API-independent description; this is also the boundary for a future Vulkan backend.
struct ScanoutTexture {
  uint32_t address, width, height, pitch, tiled, endian, swizzle, bytes;
  bool rgb10 = false;
};
ScanoutTexture DecodeScanout(const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch,
                            uint32_t active_width, uint32_t active_height);
}
