#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/logging.h>

namespace {
void ImageService(PPCContext& ctx, uint8_t* base, uint32_t address) {
  auto* function = REX_KERNEL_STATE()->function_dispatcher()->GetFunction(address);
  if (!function) {
    REXLOG_ERROR("17559: original image service {:08X} unavailable", address);
    ctx.r3.u64 = 0x80004001u;
    return;
  }
  function(ctx, base);
}
}

// Retail 17559 exports 2F2 and 803..805. These return original XUI textures,
// not encoded image buffers. Keep the original cache, async polling, PNG
// decoder, texture allocation and eviction; XamReadTile supplies local bytes.
extern "C" REX_FUNC(__imp__XamReadImage) { ImageService(ctx, base, 0x81691EE8); }
extern "C" REX_FUNC(__imp__XamInitializeGameTileCache) { ImageService(ctx, base, 0x81691DD0); }
extern "C" REX_FUNC(__imp__XamDestroyGameTileCache) { ImageService(ctx, base, 0x81691E90); }
extern "C" REX_FUNC(__imp__XamReadGameTileImage) { ImageService(ctx, base, 0x81691EC8); }
