#include <catch2/catch_test_macros.hpp>
#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ui/virtual_key.h>

REXCVAR_DECLARE(bool, mnk_mode);
using namespace rex::input;
using rex::X_RESULT;
using rex::ui::VirtualKey;

TEST_CASE("MnK delivers configured pad button transitions to keystroke consumers", "[sdk][mnk-keystroke]") {
  const bool saved = REXCVAR_GET(mnk_mode);
  struct Restore { bool value; ~Restore() { REXCVAR_SET(mnk_mode, value); } } restore{saved};
  REXCVAR_SET(mnk_mode, true);
  mnk::MnkInputDriver driver(nullptr, 0);
  std::vector<DeviceInfo> devices;
  driver.EnumerateDevices(devices);
  REQUIRE(devices.size() == 1);
  const auto id = devices[0].id;
  auto key = [&](VirtualKey vk, bool down) {
    rex::ui::KeyEvent e(nullptr, vk, 1, false, false, false, false, false);
    if (down) driver.OnKeyDown(e); else driver.OnKeyUp(e);
  };
  auto expect = [&](VirtualKey vk, uint16_t flags) {
    X_INPUT_KEYSTROKE stroke{};
    REQUIRE(driver.GetDeviceKeystroke(id, 0, &stroke) == X_ERROR_SUCCESS);
    CHECK(stroke.virtual_key == uint16_t(vk));
    CHECK(stroke.flags == flags);
  };
  auto empty = [&]() {
    X_INPUT_KEYSTROKE stroke{};
    CHECK(driver.GetDeviceKeystroke(id, 0, &stroke) == X_ERROR_EMPTY);
  };
  // A short press/release between guest polls must not disappear.
  key(VirtualKey::kSpace, true);
  key(VirtualKey::kSpace, false);
  expect(VirtualKey::kXInputPadA, X_INPUT_KEYSTROKE_KEYDOWN);
  expect(VirtualKey::kXInputPadA, X_INPUT_KEYSTROKE_KEYUP);
  empty();
  // Alternate bindings must not release A while its other key is held.
  key(VirtualKey::kSpace, true);
  expect(VirtualKey::kXInputPadA, X_INPUT_KEYSTROKE_KEYDOWN);
  key(VirtualKey::kOem1, true);
  empty();
  key(VirtualKey::kSpace, false);
  empty();
  key(VirtualKey::kOem1, false);
  expect(VirtualKey::kXInputPadA, X_INPUT_KEYSTROKE_KEYUP);
  // Changing a modifier changes the configured chord, including its release.
  key(VirtualKey::kDown, true);
  empty();
  key(VirtualKey::kShift, true);
  expect(VirtualKey::kXInputPadDpadDown, X_INPUT_KEYSTROKE_KEYDOWN);
  key(VirtualKey::kShift, false);
  expect(VirtualKey::kXInputPadDpadDown, X_INPUT_KEYSTROKE_KEYUP);
  key(VirtualKey::kDown, false);
  key(VirtualKey::kBack, true);
  expect(VirtualKey::kXInputPadB, X_INPUT_KEYSTROKE_KEYDOWN);
  rex::ui::UISetupEvent focus;
  driver.OnLostFocus(focus);
  expect(VirtualKey::kXInputPadB, X_INPUT_KEYSTROKE_KEYUP);
  key(VirtualKey::kSpace, true);
  empty();
  driver.OnGotFocus(focus);
  key(VirtualKey::kSpace, true);
  expect(VirtualKey::kXInputPadA, X_INPUT_KEYSTROKE_KEYDOWN);
}
