#include <rex/ppc/hook.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/types/io.h>
#include <rex/logging/log_category.h>
#include <rex/xam/task_context.h>
#include <cstring>

namespace {
void ImageService(PPCContext& ctx, uint8_t* base, uint32_t address) {
  auto function = REX_RUNTIME()->FindFunction(address);
  if (!function) {
    REXLOG_ERROR("17559: original image service {:08X} unavailable", address);
    ctx.r3.u64 = 0x80004001u;
    return;
  }
  (*function)(ctx, base);
}
}

// Retail 17559 exports 2F2 and 803..805. These return original XUI textures,
// not encoded image buffers. Keep the original cache, async polling, PNG
// decoder, texture allocation and eviction; XamReadTile supplies local bytes.
REX_HOOK_RAW(__imp__XamReadImage) { ImageService(ctx, base, 0x81691EE8); }
REX_HOOK_RAW(__imp__XamInitializeGameTileCache) { ImageService(ctx, base, 0x81691DD0); }
REX_HOOK_RAW(__imp__XamDestroyGameTileCache) { ImageService(ctx, base, 0x81691E90); }
REX_HOOK_RAW(__imp__XamReadGameTileImage) { ImageService(ctx, base, 0x81691EC8); }

namespace {
uint32_t EncodeOriginalPng(PPCContext& ctx, bool extended) {
  constexpr uint32_t invalid = 0x80070057;
  auto* thread = rex::kernel::KernelThread::GetCallingThread();
  if (!thread) return invalid;
  auto* memory = thread->GetAddressSpace();
  const uint32_t input = ctx.r3.u32, width = ctx.r4.u32, height = ctx.r5.u32;
  const uint32_t pitch = ctx.r6.u32, output = ctx.r7.u32, size = ctx.r8.u32;
  const uint32_t flags = extended ? ctx.r9.u32 : 0;
  const uint32_t overlapped = extended ? ctx.r10.u32 : ctx.r9.u32;
  const uint64_t row = uint64_t(width) * 4;
  const auto read = rex::kernel::kMemoryProtectRead;
  const auto rw = read | rex::kernel::kMemoryProtectWrite;
  auto span = [&](uint32_t address, uint64_t bytes, uint32_t access) {
    return address && bytes <= UINT32_MAX && memory->IsGuestRangeAccessible(address, uint32_t(bytes), access);
  };
  // Original 816B1140 packet: pixels, width, height, pitch, output, size*,
  // flags, OVERLAPPED. Bit zero preserves alpha; other flags are unverified.
  if (!rex::kernel::KernelThread::IsInThread() || !width || !height || flags > 1 || row > pitch ||
      !span(input, uint64_t(height - 1) * pitch + row, read) || !span(size, 4, rw) ||
      (overlapped && !span(overlapped, sizeof(rex::kernel::XOVERLAPPED), rw))) return invalid;
  const auto capacity = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(size));
  if (!capacity || !span(output, capacity, rw)) return invalid;
  if (!thread->CanCallGuestFunction(0x816B0BA8)) return 0x80004001;
  const auto packet = memory->SystemHeapAlloc(36);
  if (!packet) return 0x8007000E;
  struct FreePacket {
    rex::kernel::GuestAddressSpace* memory;
    uint32_t packet;
    ~FreePacket() { memory->SystemHeapFree(packet); }
  } cleanup{memory, packet};
  auto* words = memory->TranslateVirtual<rex::be_u32*>(packet);
  words[0] = input; words[1] = width; words[2] = height; words[3] = pitch;
  words[4] = output; words[5] = size; words[6] = flags; words[7] = overlapped; words[8] = 0;
  uint64_t args[]{packet, packet + 32};
  const auto result = rex::xam::RunSystemTask([&] {
    uint64_t encoded = 0;
    if (!thread->CallGuestFunction(0x816B0BA8, args, &encoded)) return uint32_t(0x80004001);
    return uint32_t(encoded);
  });
  if (overlapped) {
    // 816B10A8 completes result/extended HRESULT/byte count; 816A3128 returns
    // HRESULT_FROM_WIN32(IO_PENDING) for a submitted asynchronous request.
    auto* dispatcher = REX_INTERFACE(rex::kernel::IOverlappedDispatcher);
    REX_CHECK(dispatcher, "17559 PNG completion service unavailable");
    dispatcher->Complete(overlapped, {.result = int32_t(result) < 0 ? 1627u : 0u,
                                    .extended_error = result, .length = uint32_t(words[8])});
    return 0x800703E5;
  }
  return result;
}
}
// ExecuteTrap also reaches this context through ThreadState; do not promise
// a restrict-qualified reference across that alias.
extern "C" REX_DETAIL_ENTRY_EXPORT void __imp__XamPngEncode(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = EncodeOriginalPng(ctx, false); }
extern "C" REX_DETAIL_ENTRY_EXPORT void __imp__XamPngEncodeEx(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = EncodeOriginalPng(ctx, true); }
