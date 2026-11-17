#include <rex/chrono.h>
#include <rex/chrono/frame_timing.h>
#include <rex/chrono/frame_layout.h>
#include <cstdio>
#include <stdexcept>
#include <initializer_list>
int main() {
  using T = rex::chrono::FrameTiming;
  unsigned failed = 0;
  const auto check = [&](bool value, const char* what) { if (!value) { ++failed; std::fprintf(stderr, "%s\n", what); } };
  const double scalar = rex::Clock::GetGuestTimeScalar();
  using L = rex::chrono::FrameLayout;
  L::SetDisplayAspect(3440,1440);
  check(rex::chrono::GetNativeFrameSettings().ultrawide.Set(true).has_value(), "Native setting rejected");
  check(!L::DisplayAspect().x, "Title aspect escaped the original baseline");
  T::NotifySourceFrame();
  check(T::ReadSource().sequence == 0, "Original mode manufactured a source frame");
  check(rex::chrono::GetNativeFrameSettings().enhanced.Set(true).has_value(), "Native setting rejected");
  check(!T::Enhanced(), "Enhanced enabled without a Native backend");
  T::ActivateNative();
  check(T::Enhanced(), "Native Enhanced gate did not activate");
  check(L::DisplayAspect().x == 3440 && L::DisplayAspect().y == 1440, "Declared aspect lost physical window ratio");
  check(rex::chrono::GetNativeFrameSettings().ultrawide.Set(false).has_value(), "Native setting rejected");
  check(!L::DisplayAspect().x, "Disabled ultrawide altered guest display aspect");
  bool invalid_aspect = false;
  try { L::SetDisplayAspect(3440,0); } catch (const std::invalid_argument&) { invalid_aspect = true; }
  check(invalid_aspect, "Zero denominator was accepted for display aspect");
  for (uint32_t fps : {30u, 60u, 120u, 144u, 165u, 240u}) {
    T::SetGuestVblankRate(2 * fps);
    check(T::GuestVblankRate() == 2 * fps, "Title vblank cadence lost the divider");
    check(rex::Clock::GetGuestTimeScalar() == scalar, "FPS changed elapsed guest time");
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
  check(rex::chrono::GetNativeFrameSettings().present_rate.Set(uint32_t(144)).has_value(), "Native setting rejected");
  T::PacePresentation();
  check(T::ReadSource().sequence == 1, "Presentation manufactured a source frame");
  check(rex::chrono::GetNativeFrameSettings().enhanced.Set(false).has_value(), "Native setting rejected");
  T::NotifySourceFrame();
  check(T::ReadSource().sequence == 1, "Original mode retained Enhanced instrumentation");
  return failed ? 1 : 0;
}
