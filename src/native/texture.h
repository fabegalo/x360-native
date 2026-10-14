#pragma once
#include <cstdint>
#include <rex/graphics/xenos.h>
namespace x360n::native {
// API-independent description; this is also the boundary for a future Vulkan backend.
struct ScanoutTexture {
  uint32_t address, width, height, pitch, tiled, endian, swizzle, bytes;
};
ScanoutTexture DecodeScanout(const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch,
                            uint32_t active_width, uint32_t active_height);
}
