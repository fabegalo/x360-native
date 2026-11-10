#include "cadence.h"
#include <algorithm>
#include <cmath>
namespace x360n::native {
uint32_t GenericCadence::OnPresent(uint64_t interval_ticks, uint64_t tick_frequency, uint32_t vblank_hz,
                                   uint32_t target_fps) {
  const auto rate = [&] { return interval_ ? std::min(target_fps * interval_, kMaxRate) : 0u; };
  if (!interval_ticks || !tick_frequency || !vblank_hz) return rate();
  ratios_[next_] = float(double(interval_ticks) * vblank_hz / double(tick_frequency));
  next_ = (next_ + 1) % kWindow;
  count_ = std::min(count_ + 1, kWindow);
  if (count_ < kWindow) return rate();
  // The interval most recent presents agree on, within 15% of a vblank.
  unsigned votes[kMaxInterval + 1]{};
  for (float r : ratios_) {
    const float n = std::round(r);
    if (n >= 1 && n <= kMaxInterval && std::fabs(r - n) <= 0.15f) ++votes[unsigned(n)];
  }
  const unsigned best = unsigned(std::max_element(votes + 1, votes + kMaxInterval + 1) - votes);
  if (votes[best] * 10 >= kWindow * 8 && best != interval_) {
    interval_ = best;
    count_ = 0;  // Measure again at the new rate before another change.
  }
  return rate();
}
}
