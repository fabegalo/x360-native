#pragma once

#include <rex/kernel/heap.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/object/kernel_thread.h>

namespace x360n {
// A system app borrows the *current* guest thread for one synchronous call.
// Never retain ThreadState across frames: a title may present on another thread.
class SystemUiContext {
 public:
  explicit SystemUiContext(rex::kernel::GuestAddressSpace& memory)
      : state_(rex::kernel::ThreadState::Get()),
        saved_(*state_->GetContext()),
        thread_(rex::kernel::KernelThread::GetCallingThread()
                    ->GetGuestObject<rex::kernel::X_KTHREAD>()),
        pcr_(memory.TranslateVirtual<rex::kernel::X_KPCR*>(saved_.r13.u32)),
        process_(thread_->process_type), duplicate_(thread_->process_type_dup),
        dpc_process_(pcr_->processtype_value_in_dpc) {
    thread_->process_type = thread_->process_type_dup = 2;
    pcr_->processtype_value_in_dpc = 2;
  }
  ~SystemUiContext() {
    thread_->process_type = process_;
    thread_->process_type_dup = duplicate_;
    pcr_->processtype_value_in_dpc = dpc_process_;
    *state_->GetContext() = saved_;
    state_->GetContext()->fpscr.RestoreGuestBits(saved_.fpscr.csr);
  }
  SystemUiContext(const SystemUiContext&) = delete;
  SystemUiContext& operator=(const SystemUiContext&) = delete;
  rex::kernel::ThreadState* thread_state() const { return state_; }

 private:
  rex::kernel::ThreadState* state_;
  PPCContext saved_;
  rex::kernel::X_KTHREAD* thread_;
  rex::kernel::X_KPCR* pcr_;
  uint8_t process_, duplicate_, dpc_process_;
};
}  // namespace x360n
