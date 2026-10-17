#include "texture.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>
namespace x360n::native {
ScanoutTexture DecodeScanout(const rex::graphics::xenos::xe_gpu_texture_fetch_t& f,
                            uint32_t w, uint32_t h) {
  using namespace rex::graphics::xenos;
  const bool rgb10 = f.format == TextureFormat::k_2_10_10_10 ||
                     f.format == TextureFormat::k_2_10_10_10_AS_16_16_16_16;
  if (f.type != FetchConstantType::kTexture || f.dimension != DataDimension::k2DOrStacked ||
      (f.format != TextureFormat::k_8_8_8_8 && !rgb10) || f.stacked || f.num_format || f.exp_adjust ||
      f.sign_x != TextureSign::kUnsigned || f.sign_y != TextureSign::kUnsigned ||
      f.sign_z != TextureSign::kUnsigned || f.sign_w != TextureSign::kUnsigned ||
      f.mip_min_level != 0 || f.packed_mips) {
    std::ostringstream details;
    details << "Native: scanout requires unsigned RGBA8/RGB10A2 2D base-level texture"
            << " (type=" << unsigned(f.type) << ", dimension=" << unsigned(f.dimension)
            << ", format=" << unsigned(f.format) << ", stacked=" << f.stacked
            << ", num_format=" << f.num_format << ", exp_adjust=" << f.exp_adjust
            << ", signs=" << unsigned(f.sign_x) << '/' << unsigned(f.sign_y)
            << '/' << unsigned(f.sign_z) << '/' << unsigned(f.sign_w)
            << ", mip_min=" << f.mip_min_level << ", packed_mips=" << f.packed_mips
            << ", width=" << f.size_2d.width + 1 << ", height=" << f.size_2d.height + 1
            << ", pitch=" << (f.pitch << 5) << ", tiled=" << f.tiled << ')';
    throw std::runtime_error(details.str());
  }
  for (unsigned i = 0; i < 4; ++i)
    if (((f.swizzle >> (i * 3)) & 7) > 5)
      throw std::runtime_error("Native: reserved texture swizzle");
  const uint32_t width = f.size_2d.width + 1, height = f.size_2d.height + 1;
  const uint32_t pitch = f.pitch << 5;
  if (!pitch || pitch < width || (!f.tiled && (pitch & 63)) || width > 8192 || height > 8192)
    throw std::runtime_error("Native: invalid scanout dimensions or pitch");
  // Tiled allocation includes complete 32x32 tiles; no read crosses its bounds.
  const uint64_t bytes = uint64_t(pitch) * (f.tiled ? (height + 31) & ~31u : height) * 4;
  const uint32_t address = f.base_address << 12;
  if (!address || uint64_t(address) + bytes > 0x20000000ull)
    throw std::runtime_error("Native: scanout outside guest physical memory");
  return {address, std::min(w ? w : width, width), std::min(h ? h : height, height),
          pitch, f.tiled, uint32_t(f.endianness), f.swizzle, uint32_t(bytes), rgb10};
}
}
