// Linked into this version's bootanim DLL, at its proven initialization boundary.
#include <rex/logging/log_category.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/heap.h>

void x360n_boot1888_initialized() {
  auto* runtime = REX_RUNTIME();
  auto* thread = rex::kernel::ThreadState::Get();
  auto finish = runtime->FindFunction(0x9801A068);
  if (!thread || !finish) REX_FATAL("1888 intro: missing finish export/thread");
  auto& ctx = *thread->GetContext();
  const PPCContext saved = ctx;
  // Original export 2(false) sets object+0x28 (gradual completion request).
  // true would set +0x29 and force immediate cleanup. Never use that branch.
  ctx.r3.u64 = 0;
  (*finish)(ctx, thread->GetAddressSpace()->GetVirtualMembase());
  ctx = saved;
  ctx.fpscr.RestoreGuestBits(saved.fpscr.csr);
  REXLOG_INFO("1888 intro: initialization complete; original export 2(false) requested graceful finish");
}
