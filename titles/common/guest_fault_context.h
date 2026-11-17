#pragma once

#include <rex/platform/exception_handler.h>
#include <rex/kernel/title_runtime.h>
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
  bool Start(rex::kernel::TitleRuntime& runtime, const std::filesystem::path& path);
  void Stop();

 private:
  static bool Observe(rex::platform::Exception* exception, void* data);
  rex::kernel::TitleRuntime* runtime_ = nullptr;
  uintptr_t guest_base_ = 0;
  void* file_ = nullptr;
  std::atomic_flag writing_ = ATOMIC_FLAG_INIT;
  unsigned records_ = 0;  // guarded by writing_; bounded even if a later handler resumes
};
}  // namespace x360n
