#pragma once
#include <array>
#include <cstdint>
namespace x360n::native {
// Generic Native Enhanced frame rate. A console title presents every N guest
// vblanks (N = 1 for 60 FPS UIs such as the dashboard, 2 for the usual 30 FPS
// games). Measuring N from the present intervals and running guest vblank at
// target * N makes the title present at the selected target without a title
// profile. N only changes when the recent intervals agree on one integer 1..4:
// a title that cannot keep up produces irregular intervals and keeps the
// current multiplier, so a slow frame never raises the interrupt rate.
class GenericCadence {
 public:
  static constexpr unsigned kWindow = 32, kMaxInterval = 4;
  static constexpr uint32_t kMaxRate = 480;  // FrameTiming::SetGuestVblankRate limit.
  // Rate the guest vblank should run at after this present, given the
  // present-to-present interval in guest ticks and the vblank rate in effect
  // during it (Hz). 0 keeps the original video-mode rate.
  uint32_t OnPresent(uint64_t interval_ticks, uint64_t tick_frequency, uint32_t vblank_hz, uint32_t target_fps);
  unsigned interval() const { return interval_; }
 private:
  std::array<float, kWindow> ratios_{};
  unsigned count_ = 0, next_ = 0, interval_ = 0;
};
}
