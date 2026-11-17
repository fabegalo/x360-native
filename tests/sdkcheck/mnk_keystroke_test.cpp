#include <catch2/catch_test_macros.hpp>
#include "ui/ui_harness.h"
#include <rex/hid/input_system.h>
#include <tuple>
#include <vector>
using rex::VirtualKey;

TEST_CASE("MnK delivers configured pad button transitions to keystroke consumers", "[sdk][mnk-keystroke]") {
  rex::test_ui::UIApp app;
  app.input_drivers = "none";
  REQUIRE(app.Start());
  auto& input = app.GetInput();
  REQUIRE(input.GetSettings().mnk_mode.Set(true));
  std::vector<std::tuple<rex::SettingBase*, std::string, rex::SettingLayer>> saved;
  rex::ScopeGuard restore([&] {
    for (const auto& [setting, value, layer] : saved) {
      CHECK(setting->Reset());
      if (layer != rex::SettingLayer::kDefault) CHECK(setting->SetText(layer, value));
    }
  });
  for (auto* setting : REX_SETTINGS().Enumerate("hid"))
    if (setting->GetName().find("keybind_") != std::string::npos) {
      saved.emplace_back(setting, setting->GetText(), setting->GetLayer());
      REQUIRE(setting->SetText(rex::SettingLayer::kRuntime, ""));
    }
  // Set the original test's bindings explicitly, independently of other fixtures.
  REQUIRE(REX_SETTINGS().SetText(rex::SettingLayer::kRuntime, "hid.keybind_a", "Space,Semicolon"));
  REQUIRE(REX_SETTINGS().SetText(rex::SettingLayer::kRuntime, "hid.keybind_b", rex::VirtualKeyToString(VirtualKey::kBack)));
  REQUIRE(REX_SETTINGS().SetText(rex::SettingLayer::kRuntime, "hid.keybind_dpad_down", "Shift+Down"));
  auto key = [&](VirtualKey vk, bool down) {
    rex::test_ui::DeliverKey(down ? rex::InputEvent::Type::kKeyDown : rex::InputEvent::Type::kKeyUp, vk);
    app.Pump();
  };
  auto expect = [&](VirtualKey vk, uint16_t flags) {
    CAPTURE(uint16_t(vk), flags);
    auto stroke = input.GetKeystroke(0, rex::hid::KeystrokeSource::kPad);
    REQUIRE(stroke);
    CHECK(stroke->virtual_key == uint16_t(vk));
    CHECK(stroke->flags == flags);
  };
  auto empty = [&]() { CHECK_FALSE(input.GetKeystroke(0, rex::hid::KeystrokeSource::kPad)); };
  // A short press/release between guest polls must not disappear.
  key(VirtualKey::kSpace, true);
  key(VirtualKey::kSpace, false);
  expect(VirtualKey::kXInputPadA, 1);
  expect(VirtualKey::kXInputPadA, 2);
  empty();
  // Alternate bindings must not release A while its other key is held.
  key(VirtualKey::kSpace, true);
  expect(VirtualKey::kXInputPadA, 1);
  key(VirtualKey::kOem1, true);
  empty();
  key(VirtualKey::kSpace, false);
  empty();
  key(VirtualKey::kOem1, false);
  expect(VirtualKey::kXInputPadA, 2);
  // Changing a modifier changes the configured chord, including its release.
  key(VirtualKey::kDown, true);
  empty();
  key(VirtualKey::kShift, true);
  expect(VirtualKey::kXInputPadDPadDown, 1);
  key(VirtualKey::kShift, false);
  expect(VirtualKey::kXInputPadDPadDown, 2);
  key(VirtualKey::kDown, false);
  key(VirtualKey::kBack, true);
  expect(VirtualKey::kXInputPadB, 1);
  rex::test_ui::DeliverFocus(false); app.Pump();
  expect(VirtualKey::kXInputPadB, 2);
  key(VirtualKey::kSpace, true);
  empty();
  rex::test_ui::DeliverFocus(true); app.Pump();
  key(VirtualKey::kSpace, true);
  expect(VirtualKey::kXInputPadA, 1);
  restore.Reset();
  app.Shutdown();
}
