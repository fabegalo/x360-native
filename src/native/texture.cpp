#include "texture.h"
#include <algorithm>
#include <stdexcept>
namespace x360n::native {
ScanoutTexture DecodeScanout(const rex::graphics::xenos::xe_gpu_texture_fetch_t& f,
                            uint32_t w, uint32_t h) {
  using namespace rex::graphics::xenos;
  if (f.type != FetchConstantType::kTexture || f.dimension != DataDimension::k2DOrStacked ||
      f.format != TextureFormat::k_8_8_8_8 || f.stacked || f.num_format || f.exp_adjust ||
      f.sign_x != TextureSign::kUnsigned || f.sign_y != TextureSign::kUnsigned ||
      f.sign_z != TextureSign::kUnsigned || f.sign_w != TextureSign::kUnsigned ||
      f.mip_min_level != 0 || f.packed_mips)
    throw std::runtime_error("Native: scanout requires unsigned RGBA8 2D base-level texture");
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
          pitch, f.tiled, uint32_t(f.endianness), f.swizzle, uint32_t(bytes)};
}
}
