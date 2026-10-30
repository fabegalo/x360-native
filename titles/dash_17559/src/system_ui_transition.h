#pragma once
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/thread_state.h>

namespace dash17559 {
// XamShowGuideUI 816C3580 -> 816C2AF0 selects background mode 1;
// 816CE51C then calls 81695868(hud, mode). It starts the original timeline,
// which the normal compositor advances. Without it the background remains
// in state 0, so 81695880 has no exit animation to play.
inline uint32_t BeginGuideBackground(rex::Runtime& runtime, rex::runtime::ThreadState* thread, uint32_t hud) {
  uint64_t open[] = {hud, 1};
  return uint32_t(runtime.function_dispatcher()->ExecuteTrap(thread, 0x81695868, open, 2));
}

// Frame-by-frame equivalent of XAM 17559 816CE358..816CE3B0. The
// original HUD background owns the timeline and its completion status;
// VdSwap must keep presenting it instead of blocking inside the XAM loop.
class SystemUiCloseTransition {
 public:
  bool started() const { return started_; }

  // true: retain the system frame and input ownership for another VdSwap.
  bool Advance(rex::Runtime& runtime, rex::runtime::ThreadState* thread, uint32_t hud) {
    auto* memory = runtime.memory();
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
    auto* dispatcher = runtime.function_dispatcher();
    uint64_t close[] = {hud};
    const auto result = uint32_t(dispatcher->ExecuteTrap(thread, 0x81695880, close, 1));
    if (result != 0x8000000Au) {
      if (static_cast<int32_t>(result) < 0)
        REXLOG_ERROR("17559 system UI: original closing transition failed {:08X}", result);
      return false;
    }
    if (dispatcher->ExecuteTrap(thread, 0x81842110, nullptr, 0)) {
      uint64_t compose[] = {read(0x81AA1B18)};
      dispatcher->ExecuteTrap(thread, 0x816CCF00, compose, 1);
    }
    return true;
  }

 private:
  bool started_ = false;
};
}
