#include <rex/ppc/func.h>
#include <rex/ppc/context.h>
#include <rex/logging.h>
#include <atomic>
#include "launch_handoff.h"

// Verified D3D Swap and Resolve boundaries in this exact XEX. The
// resolved front-buffer headers are supplied by the original engine, not guessed
// addresses. Compose before resolving a known front buffer, once per swap.
namespace {
std::atomic<uint32_t> front_buffers[2]{};
std::atomic<unsigned> next_buffer{0};
std::atomic<bool> composed{false};
}
extern "C" REX_FUNC(__imp__sub_829EFB30);
extern "C" void sub_829EFB30(PPCContext& ctx, uint8_t* base) {
  // Gameplay may submit/resolve from a different guest thread than the menu.
  // Front buffers belong to the device, not the calling host thread. Observe
  // the D3D boundary also used by paths bypassing the engine's menu wrapper.
  if (ctx.r4.u32 != front_buffers[0].load() && ctx.r4.u32 != front_buffers[1].load()) {
    front_buffers[next_buffer.fetch_add(1) % 2].store(ctx.r4.u32);
    REXLOG_INFO("System UI swap boundary: device={:08X} front={:08X}", ctx.r3.u32, ctx.r4.u32);
  }
  composed.store(false);
  __imp__sub_829EFB30(ctx, base);
}

extern "C" REX_FUNC(__imp__sub_824079B8);
extern "C" void sub_824079B8(PPCContext& ctx, uint8_t* base) {
  if (ctx.r6.u32 &&
      (ctx.r6.u32 == front_buffers[0].load() || ctx.r6.u32 == front_buffers[1].load()) &&
      !composed.exchange(true)) {
    thread_local bool logged = false;
    if (!logged) {
      REXLOG_INFO("System UI resolve boundary: device={:08X} front={:08X} caller={:08X}",
          ctx.r3.u32, ctx.r6.u32, uint32_t(ctx.lr));
      logged = true;
    }
    x360n::DrawSessionSystemUi(ctx);
  }
  __imp__sub_824079B8(ctx, base);
}
