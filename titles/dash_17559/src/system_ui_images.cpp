#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xthread.h>
#include <rex/system/xio.h>
#include <rex/kernel/xam/offline_achievement_helpers.h>
#include <rex/kernel/xam/task_context.h>
#include <cstring>

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

namespace {
uint32_t EncodeOriginalPng(PPCContext& ctx, bool extended) {
  constexpr uint32_t invalid = 0x80070057;
  auto& runtime = *rex::Runtime::instance();
  auto* memory = runtime.memory();
  const uint32_t input = ctx.r3.u32, width = ctx.r4.u32, height = ctx.r5.u32;
  const uint32_t pitch = ctx.r6.u32, output = ctx.r7.u32, size = ctx.r8.u32;
  const uint32_t flags = extended ? ctx.r9.u32 : 0;
  const uint32_t overlapped = extended ? ctx.r10.u32 : ctx.r9.u32;
  const uint64_t row = uint64_t(width) * 4;
  const auto read = rex::memory::kMemoryProtectRead;
  const auto rw = read | rex::memory::kMemoryProtectWrite;
  auto span = [&](uint32_t address, uint64_t bytes, uint32_t access) {
    return rex::kernel::xam::AchievementGuestSpan(memory, address, bytes, access);
  };
  // Original 816B1140 packet: pixels, width, height, pitch, output, size*,
  // flags, OVERLAPPED. Bit zero preserves alpha; other flags are unverified.
  if (!rex::system::XThread::IsInThread() || !width || !height || flags > 1 || row > pitch ||
      !span(input, uint64_t(height - 1) * pitch + row, read) || !span(size, 4, rw) ||
      (overlapped && !span(overlapped, sizeof(rex::system::XAM_OVERLAPPED), rw))) return invalid;
  const auto capacity = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(size));
  if (!capacity || !span(output, capacity, rw)) return invalid;
  auto* dispatcher = runtime.function_dispatcher();
  if (!dispatcher->GetFunction(0x816B0BA8)) return 0x80004001;
  const auto packet = memory->SystemHeapAlloc(36);
  if (!packet) return 0x8007000E;
  struct FreePacket {
    rex::memory::Memory* memory;
    uint32_t packet;
    ~FreePacket() { memory->SystemHeapFree(packet); }
  } cleanup{memory, packet};
  auto* words = memory->TranslateVirtual<rex::be<uint32_t>*>(packet);
  words[0] = input; words[1] = width; words[2] = height; words[3] = pitch;
  words[4] = output; words[5] = size; words[6] = flags; words[7] = overlapped; words[8] = 0;
  uint64_t args[]{packet, packet + 32};
  auto* thread = rex::system::XThread::GetCurrentThread()->thread_state();
  const auto result = rex::kernel::xam::RunSystemTask([&] {
    return uint32_t(dispatcher->ExecuteTrap(thread, 0x816B0BA8, args, 2));
  });
  if (overlapped) {
    // 816B10A8 completes result/extended HRESULT/byte count; 816A3128 returns
    // HRESULT_FROM_WIN32(IO_PENDING) for a submitted asynchronous request.
    runtime.kernel_state()->CompleteOverlappedImmediateEx(overlapped,
        int32_t(result) < 0 ? 1627 : 0, result, words[8]);
    return 0x800703E5;
  }
  return result;
}
}
// ExecuteTrap also reaches this context through ThreadState; do not promise
// a restrict-qualified reference across that alias.
extern "C" void __imp__XamPngEncode(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = EncodeOriginalPng(ctx, false); }
extern "C" void __imp__XamPngEncodeEx(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = EncodeOriginalPng(ctx, true); }
