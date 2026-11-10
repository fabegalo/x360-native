#include "../../src/native/quality/cadence.h"
#include <catch2/catch_test_macros.hpp>
#include <random>
using x360n::native::GenericCadence;
namespace {
constexpr uint64_t kFreq = 50'000'000;  // Guest timebase, ticks per second.
// Present intervals of a vblank-bound title that waits `interval` vblanks at `hz`.
uint32_t Feed(GenericCadence& c, unsigned presents, unsigned interval, uint32_t hz, uint32_t fps, float jitter = 0) {
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> noise(-jitter, jitter);
  uint32_t rate = 0;
  for (unsigned i = 0; i < presents; ++i) {
    const double vblanks = interval + noise(rng);
    rate = c.OnPresent(uint64_t(vblanks * kFreq / hz), kFreq, hz, fps);
  }
  return rate;
}
}
TEST_CASE("Generic cadence keeps original timing until the title's interval is measured") {
  GenericCadence c;
  CHECK(Feed(c, GenericCadence::kWindow - 1, 1, 60, 60) == 0);
  CHECK(c.interval() == 0);
}
TEST_CASE("Generic cadence: a 60 FPS UI (one vblank per present) runs vblank at the target, not twice it") {
  GenericCadence c;
  CHECK(Feed(c, GenericCadence::kWindow, 1, 60, 60, 0.05f) == 60);
  CHECK(c.interval() == 1);
  CHECK(Feed(c, 200, 1, 60, 60, 0.05f) == 60);  // Stable at the new rate.
  GenericCadence high;
  CHECK(Feed(high, GenericCadence::kWindow, 1, 60, 144) == 144);
}
TEST_CASE("Generic cadence: a 30 FPS title (two vblanks per present) gets vblank at twice the target") {
  GenericCadence c;
  CHECK(Feed(c, GenericCadence::kWindow, 2, 60, 60, 0.1f) == 120);
  CHECK(Feed(c, 200, 2, 120, 60, 0.1f) == 120);
  GenericCadence fast;
  CHECK(Feed(fast, GenericCadence::kWindow, 2, 60, 240) == GenericCadence::kMaxRate);  // 480 Hz limit.
}
TEST_CASE("Generic cadence follows a title that changes interval (menu 1, gameplay 2)") {
  GenericCadence c;
  CHECK(Feed(c, GenericCadence::kWindow, 1, 60, 60) == 60);
  CHECK(Feed(c, GenericCadence::kWindow, 2, 60, 60) == 120);
  CHECK(Feed(c, GenericCadence::kWindow, 1, 120, 60) == 60);
}
TEST_CASE("Generic cadence ignores irregular intervals of a title that cannot keep up") {
  GenericCadence c;
  CHECK(Feed(c, GenericCadence::kWindow, 1, 60, 120) == 120);
  // At 120 Hz the title alternates 1 and 2 vblanks (about 80 FPS): no change.
  uint32_t rate = 0;
  for (unsigned i = 0; i < 200; ++i) rate = c.OnPresent((i % 2 ? 1 : 2) * kFreq / 120, kFreq, 120, 120);
  CHECK(rate == 120);
  CHECK(c.interval() == 1);
  // Load screens (no presents for a while) never change it either.
  CHECK(c.OnPresent(kFreq * 3, kFreq, 120, 120) == 120);
}
