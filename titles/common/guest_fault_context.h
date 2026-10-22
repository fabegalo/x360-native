#pragma once

#include <rex/exception_handler.h>
#include <rex/memory.h>
#include <atomic>
#include <filesystem>

namespace x360n {
// Installed before guest execution, after the runtime's MMIO handler. This
// observer never handles an exception or changes its resume context. Stop only
// after guest threads have joined: the SDK handler registry is not concurrent.
class GuestFaultContext {
 public:
  GuestFaultContext() = default;
  ~GuestFaultContext() { Stop(); }
  GuestFaultContext(const GuestFaultContext&) = delete;
  GuestFaultContext& operator=(const GuestFaultContext&) = delete;
  bool Start(rex::memory::Memory& memory, const std::filesystem::path& path);
  void Stop();

 private:
  static bool Observe(rex::arch::Exception* exception, void* data);
  rex::memory::Memory* memory_ = nullptr;
  void* file_ = nullptr;
  std::atomic_flag writing_ = ATOMIC_FLAG_INIT;
  unsigned records_ = 0;  // guarded by writing_; bounded even if a later handler resumes
};
}  // namespace x360n
