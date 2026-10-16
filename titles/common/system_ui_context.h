#pragma once

#include <rex/memory.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>

namespace x360n {
// A system app borrows the *current* guest thread for one synchronous call.
// Never retain ThreadState across frames: a title may present on another thread.
class SystemUiContext {
 public:
  explicit SystemUiContext(rex::memory::Memory& memory)
      : state_(rex::system::XThread::GetCurrentThread()->thread_state()),
        saved_(*state_->context()),
        thread_(rex::system::XThread::GetCurrentThread()
                    ->guest_object<rex::system::X_KTHREAD>()),
        pcr_(memory.TranslateVirtual<rex::system::X_KPCR*>(saved_.r13.u32)),
        process_(thread_->process_type), duplicate_(thread_->process_type_dup),
        dpc_process_(pcr_->processtype_value_in_dpc) {
    thread_->process_type = thread_->process_type_dup = 2;
    pcr_->processtype_value_in_dpc = 2;
  }
  ~SystemUiContext() {
    thread_->process_type = process_;
    thread_->process_type_dup = duplicate_;
    pcr_->processtype_value_in_dpc = dpc_process_;
    *state_->context() = saved_;
    state_->context()->fpscr.restoreGuestBits(saved_.fpscr.csr);
  }
  SystemUiContext(const SystemUiContext&) = delete;
  SystemUiContext& operator=(const SystemUiContext&) = delete;
  rex::runtime::ThreadState* thread_state() const { return state_; }

 private:
  rex::runtime::ThreadState* state_;
  PPCContext saved_;
  rex::system::X_KTHREAD* thread_;
  rex::system::X_KPCR* pcr_;
  uint8_t process_, duplicate_, dpc_process_;
};
}  // namespace x360n
