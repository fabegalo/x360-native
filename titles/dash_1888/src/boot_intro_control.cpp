// Linked into this version's bootanim DLL, at its proven initialization boundary.
#include <rex/logging.h>
#include <rex/logging/assert.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/thread_state.h>

void x360n_boot1888_initialized() {
  auto* runtime = rex::Runtime::instance();
  auto* thread = rex::runtime::ThreadState::Get();
  auto* finish = runtime->function_dispatcher()->GetFunction(0x9801A068);
  if (!thread || !finish) REX_FATAL("1888 intro: missing finish export/thread");
  auto& ctx = *thread->context();
  const PPCContext saved = ctx;
  // Original export 2(false) sets object+0x28 (gradual completion request).
  // true would set +0x29 and force immediate cleanup. Never use that branch.
  ctx.r3.u64 = 0;
  finish(ctx, runtime->memory()->virtual_membase());
  ctx = saved;
  ctx.fpscr.restoreGuestBits(saved.fpscr.csr);
  REXLOG_INFO("1888 intro: initialization complete; original export 2(false) requested graceful finish");
}
