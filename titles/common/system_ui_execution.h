#pragma once

#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/thread_registry.h>
#include <rex/logging/log_category.h>
#include <mutex>

namespace x360n {
// A title's main thread can finish before the thread running Xbox Home has
// finished TerminateTitle. Retain that exact caller until its native exit;
// guest thread termination may bypass C++ destructors entirely.
class SystemUiExecution {
 public:
  bool Begin() {
    std::lock_guard lock(mutex_);
    if (caller_) return false;
    auto* caller = rex::kernel::KernelThread::GetCallingThread();
    if (!caller) return false;
    auto& runtime = caller->GetRuntime();
    auto handle = runtime.GetObjectTable().Duplicate(caller->GetHandle());
    if (!handle) return false;
    runtime_ = &runtime;
    caller_ = *handle;
    return true;
  }
  void End() {
    std::lock_guard lock(mutex_);
    if (caller_) {
      auto closed = runtime_->GetObjectTable().Close(caller_);
      REX_CHECK(closed.has_value(), "Cannot release the system UI caller handle");
      caller_ = 0;
      runtime_ = nullptr;
    }
  }
  void WaitForCallerExit() {
    rex::kernel::ThreadPin caller;
    {
      std::lock_guard lock(mutex_);
      if (caller_) caller = runtime_->GetThreadRegistry().FindByHandle(caller_);
    }
    if (caller && !rex::kernel::KernelThread::IsInThread(caller.GetThread())) {
      auto waited = rex::platform::Wait(caller->GetThread(), false);
      REX_CHECK(waited == rex::platform::WaitResult::kSuccess, "Cannot join the system UI caller");
    }
    End();
  }
  class Scope {
   public:
    explicit Scope(SystemUiExecution& execution)
        : execution_(execution), entered_(execution.Begin()) {}
    ~Scope() { if (entered_) execution_.End(); }
    explicit operator bool() const { return entered_; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
   private:
    SystemUiExecution& execution_;
    bool entered_;
  };
 private:
  std::mutex mutex_;
  rex::kernel::TitleRuntime* runtime_ = nullptr;
  rex::kernel::X_HANDLE caller_ = 0;
};
}  // namespace x360n
