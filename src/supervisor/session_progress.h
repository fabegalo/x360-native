#pragma once
#include <chrono>

namespace x360n {
// Only a burst of short-lived children consumes this budget. User sessions
// have no lifetime transition limit; a sustained run resets the watchdog.
// Measure child lifetime, not hashing/report I/O, which can hide a tight loop.
class SessionProgress {
 public:
  bool CanContinue(std::chrono::milliseconds child_lifetime) {
    if (child_lifetime >= std::chrono::seconds(1)) {
      rapid_transitions_ = 0;
      return true;
    }
    return ++rapid_transitions_ < 32;
  }
 private:
  unsigned rapid_transitions_ = 0;
};
}
