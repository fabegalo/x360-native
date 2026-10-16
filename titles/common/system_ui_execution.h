#pragma once

#include <rex/system/xthread.h>
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
    caller_ = rex::system::retain_object(rex::system::XThread::GetCurrentThread());
    return true;
  }
  void End() {
    std::lock_guard lock(mutex_);
    caller_.reset();
  }
  void WaitForCallerExit() {
    rex::system::object_ref<rex::system::XThread> caller;
    {
      std::lock_guard lock(mutex_);
      caller = caller_;
    }
    if (caller && !rex::system::XThread::IsInThread(caller.get()))
      caller->Wait(0, 0, 0, nullptr);
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
  rex::system::object_ref<rex::system::XThread> caller_;
};
}  // namespace x360n
