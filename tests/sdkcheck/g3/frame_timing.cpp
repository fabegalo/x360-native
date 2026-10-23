#include <rex/chrono/clock.h>
#include <rex/chrono/frame_timing.h>
#include <rex/chrono/frame_layout.h>
#include <rex/cvar.h>
#include <cstdio>
#include <stdexcept>
#include <initializer_list>
int main() {
  using T = rex::chrono::FrameTiming;
  unsigned failed = 0;
  const auto check = [&](bool value, const char* what) { if (!value) { ++failed; std::fprintf(stderr, "%s\n", what); } };
  const double scalar = rex::chrono::Clock::guest_time_scalar();
  using L = rex::chrono::FrameLayout;
  L::SetDisplayAspect(3440,1440);
  REXCVAR_SET(native_ultrawide, true);
  check(!L::DisplayAspect().x, "Title aspect escaped the original baseline");
  T::NotifySourceFrame();
  check(T::ReadSource().sequence == 0, "Original mode manufactured a source frame");
  REXCVAR_SET(native_enhanced, true);
  check(!T::Enhanced(), "Enhanced enabled without a Native backend");
  T::ActivateNative();
  check(T::Enhanced(), "Native Enhanced gate did not activate");
  check(L::DisplayAspect().x == 3440 && L::DisplayAspect().y == 1440, "Declared aspect lost physical window ratio");
  REXCVAR_SET(native_ultrawide, false);
  check(!L::DisplayAspect().x, "Disabled ultrawide altered guest display aspect");
  bool invalid_aspect = false;
  try { L::SetDisplayAspect(3440,0); } catch (const std::invalid_argument&) { invalid_aspect = true; }
  check(invalid_aspect, "Zero denominator was accepted for display aspect");
  for (uint32_t fps : {30u, 60u, 120u, 144u, 165u, 240u}) {
    T::SetGuestVblankRate(2 * fps);
    check(T::GuestVblankRate() == 2 * fps, "Title vblank cadence lost the divider");
    check(rex::chrono::Clock::guest_time_scalar() == scalar, "FPS changed elapsed guest time");
  }
  bool rejected = false;
  try { T::SetGuestVblankRate(481); } catch (const std::out_of_range&) { rejected = true; }
  check(rejected, "Out-of-contract vblank cadence accepted");
  T::NotifySourceFrame();
  const auto source = T::ReadSource();
  check(source.sequence == 1, "Actual source frame not published");
  T::WakeSourceWaiters();
  check(T::ReadSource().sequence == 1 && T::ReadSource().wake != source.wake, "Idle wake manufactured a source frame");
  T::WaitSource(source, 1000);  // Already woken; must return without waiting.
  REXCVAR_SET(native_present_rate, uint32_t(144));
  T::PacePresentation();
  check(T::ReadSource().sequence == 1, "Presentation manufactured a source frame");
  REXCVAR_SET(native_enhanced, false);
  T::NotifySourceFrame();
  check(T::ReadSource().sequence == 1, "Original mode retained Enhanced instrumentation");
  return failed ? 1 : 0;
}
