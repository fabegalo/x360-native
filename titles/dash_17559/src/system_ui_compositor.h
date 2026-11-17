#pragma once

#include <rex/memory.h>
#include <rex/kernel/thread_state.h>

namespace dash17559 {
// 17559 816B7C28 compares the current KPCR thread (r13+256) with the
// compositor worker at 81AA09F0. 816C7D00 emits D1OVL_ENABLE only on that
// worker; otherwise it merely records a request for the absent XAM loop.
// Borrow that identity for synchronous composition, then restore it. The
// caller must already hold SystemUiExecution's frame scope.
class SystemCompositorContext {
 public:
  SystemCompositorContext(rex::kernel::GuestAddressSpace& memory, rex::kernel::ThreadState* thread)
      : worker_(memory.TranslateVirtual<rex::be_u32*>(0x81AA09F0)), saved_(*worker_) {
    *worker_ = rex::memory::load_and_swap<uint32_t>(
        memory.TranslateVirtual(thread->GetContext()->r13.u32 + 256));
  }
  ~SystemCompositorContext() { *worker_ = saved_; }
  SystemCompositorContext(const SystemCompositorContext&) = delete;
  SystemCompositorContext& operator=(const SystemCompositorContext&) = delete;

 private:
  rex::be_u32* worker_;
  uint32_t saved_;
};
}
