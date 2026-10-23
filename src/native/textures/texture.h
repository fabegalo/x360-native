#pragma once
#include <cstdint>
#include <bit>
#include <array>
#include <algorithm>
#include <cstring>
#include <rex/graphics/xenos.h>
#include <rex/graphics/pipeline/texture/info.h>
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
// Formats without an API-independent filterable host representation are
// expanded before upload. Conversion precedes filtering and guest swizzle.
inline unsigned ExpandedTextureComponents(rex::graphics::xenos::TextureFormat f, bool integer) {
  using namespace rex::graphics::xenos;
  f = rex::graphics::GetBaseFormat(f);
  switch (f) {
    case TextureFormat::k_1_5_5_5: return 4;
    case TextureFormat::k_5_6_5: case TextureFormat::k_6_5_5:
    case TextureFormat::k_10_11_11: case TextureFormat::k_11_11_10: return 3;
    case TextureFormat::k_32: return 1;
    case TextureFormat::k_32_32: return 2;
    case TextureFormat::k_32_32_32_32: return 4;
    case TextureFormat::k_8: case TextureFormat::k_16: return integer ? 1 : 0;
    case TextureFormat::k_8_8: case TextureFormat::k_16_16: return integer ? 2 : 0;
    case TextureFormat::k_8_8_8_8: case TextureFormat::k_16_16_16_16:
    case TextureFormat::k_2_10_10_10: case TextureFormat::k_4_4_4_4: return integer ? 4 : 0;
    default: return 0;
  }
}
inline std::array<float, 4> ExpandTextureTexel(const uint8_t* bytes,
    rex::graphics::xenos::TextureFormat f, bool sign, bool integer) {
  using namespace rex::graphics::xenos;
  f = rex::graphics::GetBaseFormat(f);
  unsigned bits[4]{}, count = ExpandedTextureComponents(f, integer);
  switch (f) {
    case TextureFormat::k_1_5_5_5: bits[0]=bits[1]=bits[2]=5; bits[3]=1; break;
    case TextureFormat::k_5_6_5: bits[0]=5; bits[1]=6; bits[2]=5; break;
    case TextureFormat::k_6_5_5: bits[0]=5; bits[1]=5; bits[2]=6; break;
    case TextureFormat::k_10_11_11: bits[0]=11; bits[1]=11; bits[2]=10; break;
    case TextureFormat::k_11_11_10: bits[0]=10; bits[1]=11; bits[2]=11; break;
    case TextureFormat::k_2_10_10_10: bits[0]=bits[1]=bits[2]=10; bits[3]=2; break;
    case TextureFormat::k_4_4_4_4: for (auto& b : bits) b=4; break;
    case TextureFormat::k_8: case TextureFormat::k_8_8: case TextureFormat::k_8_8_8_8:
      for (auto& b : bits) b=8; break;
    case TextureFormat::k_16: case TextureFormat::k_16_16: case TextureFormat::k_16_16_16_16:
      for (auto& b : bits) b=16; break;
    default: for (auto& b : bits) b=32; break;
  }
  std::array<float, 4> values{};
  unsigned offset = 0;
  for (unsigned i=0; i<count; ++i) {
    uint32_t word=0;
    for (unsigned b=0; b<bits[i]; ++b)
      word |= uint32_t((bytes[(offset+b)/8] >> ((offset+b)&7)) & 1) << b;
    offset += bits[i];
    const uint64_t limit = uint64_t(1) << bits[i];
    if (sign) {
      const int64_t value = word & (limit >> 1) ? int64_t(word) - int64_t(limit) : int64_t(word);
      values[i] = integer ? float(value) : float(std::max(-1.0, double(value) / double(std::max(uint64_t(1), (limit >> 1)-1))));
    } else values[i] = integer ? float(word) : float(double(word) / double(limit-1));
  }
  // Missing components repeat the last channel, as the guest fetch view does.
  for (unsigned i=count; i<4; ++i) values[i]=values[count-1];
  return values;
}
inline bool ExpandedTextureBlock(rex::graphics::xenos::TextureFormat f) {
  using F=rex::graphics::xenos::TextureFormat;
  return f==F::k_DXT3A || f==F::k_DXT3A_AS_1_1_1_1 || f==F::k_CTX1;
}
inline std::array<float,4> ExpandTextureBlockPixel(const uint8_t* data,
    rex::graphics::xenos::TextureFormat f, unsigned pixel, bool sign, bool integer) {
  using F=rex::graphics::xenos::TextureFormat;
  std::array<float,4> result{};
  auto component=[&](unsigned value,unsigned bits) {
    if(sign) { const int v=int(value)-(value&(1u<<(bits-1)) ? int(1u<<bits):0);
      return integer ? float(v) : PackedSnorm(value,bits); }
    return integer ? float(value) : float(value)/float((1u<<bits)-1);
  };
  if(f==F::k_CTX1) {
    uint32_t indices;std::memcpy(&indices,data+4,4);const unsigned i=(indices>>(pixel*2))&3;
    for(unsigned c=0;c<2;++c) {
      // Match the pinned CTX1 loader's integer divide before UNORM conversion.
      const unsigned a=data[c],b=data[c+2];
      const unsigned value=i==0?a:i==1?b:i==2?(2*a+b)/3:(a+2*b)/3;
      result[c]=component(value,8);
    }
    result[2]=result[3]=result[1];
  } else {
    const unsigned nibble=(data[pixel/2]>>((pixel&1)*4))&15;
    if(f==F::k_DXT3A_AS_1_1_1_1) for(unsigned c=0;c<4;++c) result[c]=float((nibble>>c)&1);
    else result.fill(component(nibble,4));
  }
  return result;
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
