#pragma once
#include <rex/kernel/title_runtime.h>
#include <rex/logging/log_category.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/thread_state.h>

namespace dash17559 {
// XamShowGuideUI 816C3580 -> 816C2AF0 selects background mode 1;
// 816CE51C then calls 81695868(hud, mode). It starts the original timeline,
// which the normal compositor advances. Without it the background remains
// in state 0, so 81695880 has no exit animation to play.
inline uint32_t BeginGuideBackground(rex::kernel::TitleRuntime& runtime, rex::kernel::ThreadState* thread, uint32_t hud) {
  auto* caller = runtime.GetThreadRegistry().GetCallingThread();
  if (!caller || !thread || thread != rex::kernel::ThreadState::Get() || thread->GetRuntime() != &runtime)
    return 0x80004001;
  uint64_t open[] = {hud, 1};
  uint64_t result = 0;
  return caller->CallGuestFunction(0x81695868, open, &result) ? uint32_t(result) : 0x80004001;
}

// Frame-by-frame equivalent of XAM 17559 816CE358..816CE3B0. The
// original HUD background owns the timeline and its completion status;
// VdSwap must keep presenting it instead of blocking inside the XAM loop.
class SystemUiCloseTransition {
 public:
  bool started() const { return started_; }

  // true: retain the system frame and input ownership for another VdSwap.
  bool Advance(rex::kernel::TitleRuntime& runtime, rex::kernel::ThreadState* thread, uint32_t hud) {
    auto* caller = runtime.GetThreadRegistry().GetCallingThread();
    if (!caller || !thread || thread != rex::kernel::ThreadState::Get() || thread->GetRuntime() != &runtime) {
      REXLOG_ERROR("17559 system UI: closing transition has no matching guest caller");
      return false;
    }
    auto* memory = thread->GetAddressSpace();
    auto read = [&](uint32_t address) {
      return rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    };
    if (!started_) {
      started_ = true;
      // host +220 suppresses the fade change in the original closing path.
      if (!read(0x81AA21BC))
        rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(0x81AA21B4), read(0x815F86F8));
      // Do not write +216: 816C7F20 advances the current fade while composing.
    }
    uint64_t close[] = {hud};
    uint64_t returned = 0;
    if (!caller->CallGuestFunction(0x81695880, close, &returned)) {
      REXLOG_ERROR("17559 system UI: original closing transition is unavailable");
      return false;
    }
    const auto result = uint32_t(returned);
    if (result != 0x8000000Au) {
      if (static_cast<int32_t>(result) < 0)
        REXLOG_ERROR("17559 system UI: original closing transition failed {:08X}", result);
      return false;
    }
    uint64_t ready = 0;
    if (!caller->CallGuestFunction(0x81842110, {}, &ready)) {
      REXLOG_ERROR("17559 system UI: original compositor readiness callback is unavailable");
      return false;
    }
    if (ready) {
      uint64_t compose[] = {read(0x81AA1B18)};
      if (!caller->CallGuestFunction(0x816CCF00, compose)) {
        REXLOG_ERROR("17559 system UI: original compositor callback is unavailable");
        return false;
      }
    }
    return true;
  }

 private:
  bool started_ = false;
};
}
