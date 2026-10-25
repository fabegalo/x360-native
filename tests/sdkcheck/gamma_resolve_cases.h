#pragma once
// Independent numerical oracle for gamma and full fixed16 resolves. Uses an enumerated decode
// table, a search over PWL interval boundaries, and rational normalization;
// none of the production shader functions are called by this oracle.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/util/draw.h>
#include <string>
#include <stdexcept>
#include <vector>

namespace gamma_probe {
using namespace rex::graphics;
inline constexpr unsigned kEdramBytes = 10 * 1024 * 1024 * 16;
inline constexpr unsigned kDestBytes = 4 * 1024 * 1024;
inline constexpr unsigned kWidth = 32, kHeight = 8;

inline constexpr auto kDecode = [] {
  std::array<unsigned, 256> table{};
  // PWL linear codes at the start of each region. The extra regions express
  // the console's low-bit replication, independently of the shader's shifts.
  constexpr unsigned first[] = {0, 64, 96, 128, 192, 208, 224, 240, 256};
  constexpr unsigned code[] = {0, 64, 128, 257, 516, 645, 774, 903};
  constexpr unsigned step[] = {1, 2, 4, 4, 8, 8, 8, 8};
  for (unsigned s = 0; s < 8; ++s)
    for (unsigned g = first[s]; g < first[s + 1]; ++g)
      table[g] = code[s] + (g - first[s]) * step[s];
  return table;
}();
inline constexpr auto kThreshold = [] {
  std::array<unsigned, 256> table{};
  constexpr unsigned first[] = {0, 64, 96, 192, 256},
                     code[] = {0, 64, 128, 512}, step[] = {1, 2, 4, 8};
  for (unsigned s = 0; s < 4; ++s)
    for (unsigned g = first[s]; g < first[s + 1]; ++g)
      table[g] = code[s] + (g - first[s]) * step[s];
  return table;
}();
inline float Quantize(unsigned sum, unsigned count) {
  const unsigned mean = sum / count;
  const unsigned g =
      unsigned(std::upper_bound(kThreshold.begin(), kThreshold.end(), mean) -
               kThreshold.begin() - 1);
  const unsigned n = kDecode[g];
  // Exact rational truncation to the console's 24 fractional bits.
  return float(double((uint64_t(n) * 16777216) / 1023) / 16777216.0);
}
inline uint16_t Half(float f) {
  // IEEE binary16, positive finite fixture values, ties to even.
  if (std::signbit(f))
    return uint16_t(0x8000u | Half(-f));
  unsigned b = std::bit_cast<unsigned>(f), e = (b >> 23) & 255,
           mant = b & 0x7fffff;
  if (!e)
    return 0;
  int he = int(e) - 127 + 15;
  if (he <= 0) {
    if (he < -10)
      return 0;
    unsigned v = mant | 0x800000, shift = unsigned(14 - he);
    unsigned q = v >> shift, r = v & ((1u << shift) - 1);
    return uint16_t(
        q + (r > (1u << (shift - 1)) || (r == (1u << (shift - 1)) && (q & 1))));
  }
  unsigned q = mant >> 13, r = mant & 8191;
  return uint16_t((he << 10) + q + (r > 4096 || (r == 4096 && (q & 1))));
}
inline uint16_t HalfTowardZero(float f) {
  // D3D floating-point conversion (including f32tof16) truncates. Derive
  // independently with frexp, rather than sharing the shader intrinsic.
  if (std::signbit(f))
    return uint16_t(0x8000u | HalfTowardZero(-f));
  if (f == 0)
    return 0;
  int exponent = 0;
  double mantissa = std::frexp(double(f), &exponent);
  int encoded = exponent + 14;
  if (encoded <= 0)
    return uint16_t(std::ldexp(double(f), 24));
  return uint16_t((encoded << 10) + unsigned((mantissa * 2 - 1) * 1024));
}
struct Case {
  unsigned msaa = 0, select = 0, log = 2, scale = 1;
  unsigned width = kWidth, height = kHeight;
  int bias = 0;
  xenos::ColorFormat format = xenos::ColorFormat::k_8_8_8_8;
  bool gamma = true, source64 = false, fixed16 = false, swap = false,
       partial = false, array = false, uniform_fixed16 = false;
  bool uniform_rgb565 = false;
  unsigned slice = 0, endian = 0;
  std::string Name() const {
    return "msaa=" + std::to_string(1u << msaa) +
           " select=" + std::to_string(select) +
           " dest=" + std::to_string(unsigned(format)) +
           " bias=" + std::to_string(bias) + " scale=" + std::to_string(scale) +
           " gamma=" + std::to_string(gamma) +
           " source64=" + std::to_string(source64) +
           " fixed16=" + std::to_string(fixed16) + " endian=" + std::to_string(endian) +
           " swap=" + std::to_string(swap) +
           " partial=" + std::to_string(partial) +
           " slice=" + std::to_string(slice);
  }
  unsigned Pitch() const {
    return (((std::max(160u, width) << unsigned(msaa == 2)) + 79) / 80)
           << unsigned(source64);
  }
  unsigned DestPitch() const {
    return std::max(64u, (width + (partial ? 8 : 0) + 31) & ~31u);
  }
  unsigned DestHeight() const {
    return std::max(64u, (height + (partial ? 8 : 0) + 31) & ~31u);
  }
  draw_util::ResolveCopyShaderConstants Constants() const {
    draw_util::ResolveCopyShaderConstants c{};
    auto &e = c.dest_relative.edram_info;
    e.pitch_tiles = Pitch();
    e.msaa_samples = xenos::MsaaSamples(msaa);
    e.base_tiles = 2047;
    e.format = unsigned(
        fixed16    ? (source64 ? xenos::ColorRenderTargetFormat::k_16_16_16_16
                               : xenos::ColorRenderTargetFormat::k_16_16)
        : source64 ? xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT
        : gamma    ? xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA
                   : xenos::ColorRenderTargetFormat::k_8_8_8_8);
    e.format_is_64bpp = source64;
    auto &xy = c.dest_relative.coordinate_info;
    xy.width_div_8 = width / 8;
    xy.edram_offset_x_div_8 = partial;
    xy.edram_offset_y_div_8 = partial;
    xy.draw_resolution_scale_x = xy.draw_resolution_scale_y = scale;
    auto &dest = c.dest_relative.dest_info;
    dest.copy_dest_format = format;
    dest.copy_dest_exp_bias = bias;
    dest.copy_dest_swap = swap;
    dest.copy_dest_array = array;
    dest.copy_dest_slice = slice;
    dest.copy_dest_endian = xenos::Endian128(endian);
    auto &dc = c.dest_relative.dest_coordinate_info;
    dc.pitch_aligned_div_32 = DestPitch() / 32;
    dc.height_aligned_div_32 = DestHeight() / 32;
    dc.copy_sample_select = xenos::CopySampleSelect(select);
    dc.offset_x_div_8 = dc.offset_y_div_8 = partial;
    c.dest_base = scale == 1 ? 4096 : 0;
    return c;
  }
  unsigned Kernel() const { return log; }
  std::array<unsigned, 3> Groups() const {
    const unsigned threadPixels = log == 0 ? 8 : log == 4 ? 2 : 4;
    return {(width * scale + 8 * threadPixels - 1) / (8 * threadPixels),
            (height * scale + 7) / 8, 1};
  }
};
inline unsigned Channel(unsigned p, unsigned sample, unsigned channel,
                        unsigned subx, unsigned suby) {
  const unsigned g = sample == 0   ? p
                     : sample == 1 ? p * 17 + 51
                     : sample == 2 ? 255 - p
                                   : p * 73 + 19;
  return channel == 3 ? (p * 29 + 17 * sample + subx * 3 + suby * 5) & 255
                      : (g + 37 * channel + subx * 3 + suby * 5) & 255;
}
inline int16_t FixedSource(unsigned p, unsigned sample, unsigned channel) {
  constexpr int16_t codes[] = {-32768, -32767, -16384, -1025, -1024, -1,
                               0,      1,      1023,   1024,  16384, 32767};
  return codes[(p + sample * 3 + channel * 5) % 12];
}
inline unsigned FixtureChannel(const Case& c, unsigned p, unsigned sample,
                               unsigned channel, unsigned subx, unsigned suby) {
  constexpr unsigned uniform[] = {64, 128, 192, 255};
  return c.uniform_rgb565 ? uniform[channel]
                         : Channel(p, sample, channel, subx, suby);
}
inline int16_t FixedCode(const Case& c, unsigned p, unsigned sample, unsigned channel) {
  constexpr int16_t uniform[] = {1024, 512, 256, 2048};
  return c.uniform_fixed16 ? uniform[channel] : FixedSource(p, sample, channel);
}
inline unsigned EdramAddress(const Case &c, unsigned x, unsigned y,
                             unsigned sample) {
  unsigned gx = x / c.scale + (c.partial ? 8 : 0),
           gy = y / c.scale + (c.partial ? 8 : 0);
  unsigned sx = (gx << unsigned(c.msaa == 2)) + (sample >> 1),
           sy = (gy << unsigned(c.msaa >= 1)) + (sample & 1);
  sx = sx * c.scale + x % c.scale;
  sy = sy * c.scale + y % c.scale;
  unsigned tw = (c.source64 ? 40 : 80) * c.scale, th = 16 * c.scale;
  unsigned tile = (2047 + (sy / th) * c.Pitch() + sx / tw) & 2047;
  return (tile * 1280 * c.scale * c.scale +
          ((sy % th) * tw + sx % tw) * (c.source64 ? 2 : 1)) *
         4;
}
inline void Fill(const Case &c, void *input) {
  auto *bytes = static_cast<unsigned char *>(input);
  for (unsigned y = 0; y < c.height * c.scale; ++y)
    for (unsigned x = 0; x < c.width * c.scale; ++x)
      for (unsigned sample = 0; sample < (1u << c.msaa); ++sample) {
        unsigned p = (y / c.scale) * c.width + x / c.scale;
        unsigned channels[4];
        for (unsigned ch = 0; ch < 4; ++ch)
          channels[ch] = FixtureChannel(c, p, sample, ch, x % c.scale, y % c.scale);
        auto address = EdramAddress(c, x, y, sample);
        if (address + (c.source64 ? 8u : 4u) > kEdramBytes)
          throw std::runtime_error("fixed16 fixture EDRAM capacity");
        if (c.fixed16) {
          int16_t values[4];
          for (unsigned ch = 0; ch < 4; ++ch)
            values[ch] = FixedCode(c, p, sample, ch);
          std::memcpy(bytes + address, values, c.source64 ? 8 : 4);
        } else if (c.source64) {
          uint16_t values[4];
          for (unsigned ch = 0; ch < 4; ++ch)
            values[ch] = Half(float(channels[ch]) * (1.f / 255));
          std::memcpy(bytes + address, values, 8);
        } else {
          unsigned value = channels[0] | channels[1] << 8 | channels[2] << 16 |
                           channels[3] << 24;
          std::memcpy(bytes + address, &value, 4);
        }
      }
}
inline std::array<float, 4> Values(const Case &c, unsigned x, unsigned y) {
  unsigned first = c.select < 4    ? c.select
                   : c.select == 5 ? 2
                                   : 0,
           count = c.select < 4    ? 1
                   : c.select == 6 ? 4
                                   : 2;
  std::array<float, 4> out{};
  unsigned p = (y / c.scale) * c.width + x / c.scale;
  for (unsigned ch = 0; ch < 4; ++ch) {
    unsigned sum = 0;
    float fixed = 0;
    for (unsigned s = first; s < first + count; ++s) {
      auto g = FixtureChannel(c, p, s, ch, x % c.scale, y % c.scale);
      sum += kDecode[g];
      float v = float(g) * (1.f / 255);
      if (c.fixed16)
        v = (c.source64 || ch < 2) ? float(FixedCode(c, p, s, ch)) / 1024.f
            : (c.log == 0 && ch == 3) ? 1.f : 0.f;
      else if (c.source64) {
        auto h = Half(v);
        unsigned e = (h >> 10) & 31, m = h & 1023;
        v = e ? std::ldexp(float(1024 + m), int(e) - 25)
              : std::ldexp(float(m), -24);
      }
      fixed += v;
    }
    out[ch] =
        std::ldexp(c.gamma && !c.source64 && ch < 3 ? Quantize(sum, count)
                                                    : fixed / float(count),
                   c.bias);
  }
  if (c.swap && c.log != 0)
    std::swap(out[0], out[2]);
  return out;
}
inline unsigned Fixed(float f, unsigned bits) {
  return unsigned(std::clamp(f, 0.f, 1.f) * float((1u << bits) - 1) + .5f);
}
// RGB565 reference from the uploaded integer codes, using rational arithmetic
// throughout averaging, exponent bias and quantization. No shader float math.
inline unsigned Rgb565Channel(const Case& c, unsigned x, unsigned y,
                             unsigned channel, unsigned bits) {
  const unsigned first = c.select < 4 ? c.select : c.select == 5 ? 2 : 0;
  const unsigned count = c.select < 4 ? 1 : c.select == 6 ? 4 : 2;
  uint64_t numerator = 0;
  uint64_t denominator = uint64_t(count) * (c.source64 ? (1u << 24) : 255u);
  for (unsigned s = first; s < first + count; ++s) {
    const unsigned code = FixtureChannel(c, (y / c.scale) * c.width + x / c.scale,
                                  s, channel, x % c.scale, y % c.scale);
    if (c.source64) {
      const unsigned half = Half(float(code) * (1.f / 255));
      const unsigned exponent = half >> 10, mantissa = half & 1023;
      numerator += exponent ? uint64_t(1024 + mantissa) << (exponent - 1)
                            : mantissa;
    } else {
      numerator += code;
    }
  }
  if (c.bias >= 0) numerator <<= c.bias;
  else denominator <<= -c.bias;
  const unsigned maximum = (1u << bits) - 1;
  return numerator >= denominator ? maximum
      : unsigned((numerator * maximum + denominator / 2) / denominator);
}
inline unsigned Rgb565(const Case& c, unsigned x, unsigned y) {
  return Rgb565Channel(c, x, y, c.swap ? 2 : 0, 5) |
         (Rgb565Channel(c, x, y, 1, 6) << 5) |
         (Rgb565Channel(c, x, y, c.swap ? 0 : 2, 5) << 11);
}
inline unsigned DestinationAddress(const Case &c, unsigned x, unsigned y) {
  unsigned hx = x + (c.partial ? 8 * c.scale : 0),
           hy = y + (c.partial ? 8 * c.scale : 0);
  unsigned gx = hx / c.scale, gy = hy / c.scale;
  unsigned address =
      c.array ? texture_util::GetTiledOffset3D(gx, gy, c.slice, c.DestPitch(),
                                               c.DestHeight(), c.log)
              : texture_util::GetTiledOffset2D(gx, gy, c.DestPitch(), c.log);
  if (c.scale == 1)
    return address + 4096;
  const unsigned block = c.log ? 16 : 8, pixels = block >> c.log;
  unsigned hostx = ((address & (block - 1)) >> c.log) * c.scale + hx % c.scale;
  return (address & ~(block - 1)) * c.scale * c.scale +
         ((hostx / pixels) * c.scale + hy % c.scale) * block +
         (hostx % pixels) * (1u << c.log);
}
inline std::vector<unsigned char> Expected(const Case &c, bool dxbc = false) {
  std::vector<unsigned char> bytes(kDestBytes, 0xcd);
  for (unsigned y = 0; y < c.height * c.scale; ++y)
    for (unsigned x = 0; x < c.width * c.scale; ++x) {
      auto v = Values(c, x, y);
      auto address = DestinationAddress(c, x, y);
      if (c.log == 0) {
        bytes[address] = Fixed(v[c.swap ? 3 : 0], 8);
      } else if (c.log == 1) {
        unsigned packed = c.format == xenos::ColorFormat::k_8_8
                              ? Fixed(v[0], 8) | (Fixed(v[1], 8) << 8)
                              : Fixed(v[0], 5) | (Fixed(v[1], 6) << 5) |
                                    (Fixed(v[2], 5) << 11);
        if (c.format == xenos::ColorFormat::k_5_6_5 && !c.gamma && !c.fixed16)
          packed = Rgb565(c, x, y);
        uint16_t word = uint16_t(packed);
        std::memcpy(bytes.data() + address, &word, 2);
      } else if (c.log == 2) {
        unsigned packed =
            c.format == xenos::ColorFormat::k_2_10_10_10
                ? Fixed(v[0], 10) | (Fixed(v[1], 10) << 10) |
                      (Fixed(v[2], 10) << 20) | (Fixed(v[3], 2) << 30)
                : Fixed(v[0], 8) | (Fixed(v[1], 8) << 8) |
                      (Fixed(v[2], 8) << 16) | (Fixed(v[3], 8) << 24);
        std::memcpy(bytes.data() + address, &packed, 4);
      } else if (c.log == 3) {
        uint16_t h[4];
        for (unsigned ch = 0; ch < 4; ++ch)
          h[ch] = dxbc ? HalfTowardZero(v[ch]) : Half(v[ch]);
        std::memcpy(bytes.data() + address, h, 8);
      } else
        std::memcpy(bytes.data() + address, v.data(), 16);
    }
    // Canary's resolve stores apply endian conversion within the destination
    // element width: 8-bit ignores endian, 16-bit accepts only 8in16,
    // 32-bit ignores 8in64/8in128, 64-bit ignores 8in128.
    // This is distinct from a generic 128-bit buffer byte swap.
    if (c.log && c.endian &&
        (c.log == 1 ? c.endian == 1 : c.endian <= c.log + 1)) {
    const unsigned mask = c.endian == 1   ? 1
                          : c.endian == 2 ? 3
                          : c.endian == 3 ? 2
                          : c.endian == 4 ? 7
                                          : 15;
    auto swapped = bytes;
    for (unsigned i = 0; i < kDestBytes; ++i)
      swapped[i ^ mask] = bytes[i];
    return swapped;
  }
  return bytes;
}
inline std::vector<Case> Cases() {
  std::vector<Case> out;
  const std::array<std::pair<xenos::ColorFormat, unsigned>, 7> formats = {
      {{xenos::ColorFormat::k_8, 0},
       {xenos::ColorFormat::k_8_8, 1},
       {xenos::ColorFormat::k_5_6_5, 1},
       {xenos::ColorFormat::k_8_8_8_8, 2},
       {xenos::ColorFormat::k_2_10_10_10, 2},
       {xenos::ColorFormat::k_16_16_16_16_FLOAT, 3},
       {xenos::ColorFormat::k_32_32_32_32_FLOAT, 4}}};
  for (unsigned msaa : {0u, 1u, 2u})
    for (unsigned select = 0; select < 7; ++select) {
      if ((msaa == 0 && select != 0) ||
          (msaa == 1 && select != 0 && select != 1 && select != 4))
        continue;
      for (auto [format, log] : formats)
        for (int bias : {-2, 0, 2})
          for (bool swap : {false, true}) {
            Case c;
            c.msaa = msaa;
            c.select = select;
            c.format = format;
            c.log = log;
            c.bias = bias;
            c.swap = swap;
            c.partial = (select + log) % 2;
            out.push_back(c);
            if (select == 0 || select == 6) {
              c.scale = 2;
              out.push_back(c);
              c.scale = 1;
            }
            if (select == 0 || select == 4 || select == 6) {
              c.gamma = false;
              out.push_back(c);
              c.source64 = true;
              out.push_back(c);
            }
          }
    }
  for (unsigned slice : {1u, 4u, 7u}) {
    Case c;
    c.array = true;
    c.slice = slice;
    c.partial = true;
    c.msaa = 2;
    c.select = 6;
    out.push_back(c);
  }
  for (unsigned msaa : {0u, 1u, 2u})
    for (bool wide : {false, true})
      for (unsigned log : {2u, 3u, 4u})
        for (int bias : {-1, 0, 2}) {
          Case c;
          c.msaa = msaa;
          c.select = msaa == 0 ? 0 : msaa == 1 ? 4 : 6;
          c.log = log;
          c.format = log == 2   ? xenos::ColorFormat::k_8_8_8_8
                     : log == 3 ? xenos::ColorFormat::k_16_16_16_16_FLOAT
                                : xenos::ColorFormat::k_32_32_32_32_FLOAT;
          c.bias = bias;
          c.source64 = wide;
          c.fixed16 = true;
          c.gamma = false;
          c.partial = true;
          out.push_back(c);
        }
  for (unsigned msaa : {0u, 1u, 2u})
    for (int bias : {-32, 31})
      for (auto [format, log] : formats) {
        if (log == 3)
          continue; // Native FP16 overflow behavior is a separate API contract.
        Case c;
        c.msaa = msaa;
        c.select = msaa == 0 ? 0 : msaa == 1 ? 4 : 6;
        c.bias = bias;
        c.format = format;
        c.log = log;
        out.push_back(c);
      }
  for (unsigned log : {2u, 3u, 4u})
    for (unsigned endian = 1; endian <= 5; ++endian) {
      Case c;
      c.msaa = 2;
      c.select = 6;
      c.log = log;
      c.format = log == 2   ? xenos::ColorFormat::k_8_8_8_8
                 : log == 3 ? xenos::ColorFormat::k_16_16_16_16_FLOAT
                            : xenos::ColorFormat::k_32_32_32_32_FLOAT;
      c.endian = endian;
      c.partial = true;
      c.swap = endian & 1;
      out.push_back(c);
    }
  return out;
}
inline std::vector<Case> Fixed16ExtendedCases() {
  const std::pair<xenos::ColorFormat, unsigned> formats[] = {
      {xenos::ColorFormat::k_8, 0}, {xenos::ColorFormat::k_8_8, 1},
      {xenos::ColorFormat::k_8_8_8_8, 2},
      {xenos::ColorFormat::k_2_10_10_10, 2},
      {xenos::ColorFormat::k_16_16_16_16_FLOAT, 3},
      {xenos::ColorFormat::k_32_32_32_32_FLOAT, 4}};
  std::vector<Case> cases;
  for (unsigned msaa : {0u, 1u, 2u}) {
    std::vector<unsigned> selectors{0};
    if (msaa == 1) selectors = {0, 1, 4};
    if (msaa == 2) selectors = {0, 1, 2, 3, 4, 5, 6};
    for (auto select : selectors)
      for (bool wide : {false, true})
        for (auto [format, log] : formats)
          for (unsigned scale : {1u, 2u, 3u, 4u}) {
            Case c;
            c.gamma = false;
            c.fixed16 = true;
            c.msaa = msaa;
            c.select = select;
            c.source64 = wide;
            c.format = format;
            c.log = log;
            c.scale = scale;
            c.bias = int((select + scale) % 3) - 1;
            c.swap = (select + scale) & 1;
            c.partial = true;
            c.endian = (select + scale) % 6;
            c.array = scale == 1;
            c.slice = 7;
            cases.push_back(c);
          }
    for (bool wide : {false, true})
      for (int bias : {-32, -16, -8, -1, 0, 2, 8, 31})
        for (auto [format, log] : formats) {
          if (log == 3 && bias == 31) continue;
          Case c;
          c.gamma = false;
          c.fixed16 = true;
          c.msaa = msaa;
          c.select = msaa == 0 ? 0 : msaa == 1 ? 4 : 6;
          c.source64 = wide;
          c.format = format;
          c.log = log;
          c.bias = bias;
          cases.push_back(c);
        }
  }
  return cases;
}
inline std::vector<Case> Rgb565ExtendedCases() {
  std::vector<Case> cases;
  for (unsigned msaa : {0u, 1u, 2u}) {
    const std::vector<unsigned> selections = msaa == 0 ? std::vector<unsigned>{0}
        : msaa == 1 ? std::vector<unsigned>{0, 1, 4}
                    : std::vector<unsigned>{0, 1, 2, 3, 4, 5, 6};
    for (unsigned select : selections)
      for (bool wide : {false, true})
        for (bool swap : {false, true})
          for (unsigned scale : {1u, 2u, 3u, 4u}) {
            Case c;
            c.gamma = false;
            c.format = xenos::ColorFormat::k_5_6_5;
            c.log = 1;
            c.msaa = msaa;
            c.select = select;
            c.source64 = wide;
            c.swap = swap;
            c.scale = scale;
            c.width = 96;
            c.height = 40;
            c.partial = true;
            c.array = scale == 1;
            c.slice = 7;
            c.endian = (select + scale + unsigned(swap)) % 6;
            c.bias = int((select + scale) % 3) - 1;
            cases.push_back(c);
          }
    for (bool wide : {false, true})
      for (bool swap : {false, true})
        for (int bias : {-32, -16, -8, -1, 0, 2, 8, 31}) {
          Case c;
          c.gamma = false;
          c.format = xenos::ColorFormat::k_5_6_5;
          c.log = 1;
          c.msaa = msaa;
          c.select = msaa == 0 ? 0 : msaa == 1 ? 4 : 6;
          c.source64 = wide;
          c.swap = swap;
          c.bias = bias;
          cases.push_back(c);
        }
  }
  return cases;
}
} // namespace gamma_probe
