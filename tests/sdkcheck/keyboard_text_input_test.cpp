#include <catch2/catch_test_macros.hpp>
#include "ui/ui_harness.h"
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include "kernel/title_state.h"
#include <rex/kernel/title_runtime.h>
#include <rex/xam/input.h>
#include <rex/kernel/heap.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>

namespace {
class OverlayTextListener final : public rex::ui::IUIEventListener {
 public:
  void OnUIEvent(const rex::ui::UIEvent&) override {}
  void OnInputEvent(rex::InputEvent& event) override {
    if (event.type == rex::InputEvent::Type::kKeyChar) { ++characters; event.handled = true; }
  }
  unsigned characters = 0;
};
}
TEST_CASE("Original keyboard owns window text ahead of overlays and survives mouse release",
          "[sdk][keyboard-text-input]") {
  rex::test_ui::UIApp app;
  app.input_drivers = "none";
  REQUIRE(app.Start());
  auto& keyboard = app.GetInput();
  REQUIRE(keyboard.GetSettings().mnk_mode.Set(true));
  REQUIRE(keyboard.GetSettings().mnk_mouse.Set(true));
  OverlayTextListener overlay;
  auto listener = app.GetUI().AddEventListener(overlay);
  keyboard.SetKeyboardTranslation(false);
  app.GetUI().SetSystemTextInputActive(true); app.Pump();
  CHECK(rex::test_ui::g_ui.text_input);
  rex::test_ui::Deliver([](rex::ui::IUIEventListener& target) {
    rex::InputEvent event{.type=rex::InputEvent::Type::kKeyChar, .character=0xE7};
    target.OnInputEvent(event);
  });
  app.Pump();
  auto stroke = keyboard.GetKeystroke(0, rex::hid::KeystrokeSource::kKeyboard);
  REQUIRE(stroke); CHECK(stroke->unicode == 0xE7); CHECK(overlay.characters == 0);
  keyboard.SetKeyboardTranslation(true);
  app.GetUI().SetSystemTextInputActive(false); app.Pump();
  CHECK_FALSE(rex::test_ui::g_ui.text_input);
  rex::test_ui::Deliver([](rex::ui::IUIEventListener& target) {
    rex::InputEvent event{.type=rex::InputEvent::Type::kKeyChar, .character='a'};
    target.OnInputEvent(event);
  });
  app.Pump(); CHECK(overlay.characters == 1);
  CHECK_FALSE(keyboard.GetKeystroke(0, rex::hid::KeystrokeSource::kKeyboard));
  listener.Reset(); app.Shutdown();
}

TEST_CASE("Original keyboard text mode accepts UTF16 without controller shortcuts or title leakage",
          "[sdk][keyboard-text-input]") {
  rex::test::KernelFixture fixture;
  auto& runtime = fixture.GetRuntime();
  auto& keyboard = fixture.GetInput();
  REQUIRE(rex::kernel::StateOf(runtime).AttachSystems(nullptr, nullptr, nullptr, &keyboard, nullptr, {}));
  REQUIRE(keyboard.GetSettings().mnk_mode.Set(true));
  REQUIRE(keyboard.GetSettings().guide_chord_ms.Set(0));
  auto* xam = &REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI();
  REQUIRE(xam->RegisterSystemApp({0x1000,0xFF,0x917DE3C8,11})==0);
  REQUIRE(xam->BeginSystemUi(0x1000,0xFF,0));
  REQUIRE(xam->EnableSystemUiInput(0,true));REQUIRE(xam->SetSystemUiVisible(0x1000));
  auto* mem = &rex::kernel::GetAddressSpace(runtime);
  const auto out = mem->SystemHeapAlloc(64); REQUIRE(out);
  auto* stroke = mem->TranslateVirtual<rex::xam::X_INPUT_KEYSTROKE*>(out);
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
  const auto event = [&](uint16_t code) { rex::InputEvent e{}; e.key=rex::VirtualKey(code); return e; };
  const auto send = [&](rex::InputEvent& e, rex::InputEvent::Type type) { e.type=type; keyboard.OnInputEvent(e); };
  const auto hud = [&] { return caller.XAM("XamInputGetKeystrokeHudEx", {0, 0x20000000, out}); };
  const auto mode = [&](bool translate) { caller.XAM("XamInputSetKeyboardTranslationHud", {translate}); };
  mode(false); // Original vk sends 0 on focus, 1 when leaving/destroying.
  for(const auto value : {uint16_t(' '),uint16_t(0xE7),uint16_t(0xD83D),uint16_t(0xDE00)}) {
    if (value != 0xDE00) { auto key=event(value); key.character=value == 0xD83D ? 0x1F600 : value; send(key, rex::InputEvent::Type::kKeyChar); }
    CHECK(caller.XAM("XamInputGetKeystroke", {0, 1, out})==4306);
    REQUIRE(hud()==0);CHECK(uint16_t(stroke->unicode)==value);
    CHECK((uint16_t(stroke->flags)&1)!=0);CHECK(stroke->user_index==0);
  }
  auto back=event(8);send(back, rex::InputEvent::Type::kKeyDown);
  REQUIRE(hud()==0);CHECK(uint16_t(stroke->virtual_key)==8);CHECK(uint16_t(stroke->unicode)==0);
  send(back, rex::InputEvent::Type::kKeyUp);REQUIRE(hud()==0);CHECK((uint16_t(stroke->flags)&2)!=0);
  auto space=event(32);send(space, rex::InputEvent::Type::kKeyDown);
  auto pad=keyboard.GetState(0);REQUIRE(pad);
  CHECK(uint16_t(pad->state.buttons)==0);send(space, rex::InputEvent::Type::kKeyUp);
  auto stale=event('x');stale.character='x';send(stale, rex::InputEvent::Type::kKeyChar);mode(true);CHECK(hud()==4306);
  send(space, rex::InputEvent::Type::kKeyDown);REQUIRE(hud()==0);
  CHECK(uint16_t(stroke->virtual_key)==uint16_t(rex::VirtualKey::kXInputPadA));
  send(space, rex::InputEvent::Type::kKeyUp);REQUIRE(hud()==0);
  // Confirm held across UI closure must not become a game START repeat.
  mode(false);auto enter=event(13);send(enter, rex::InputEvent::Type::kKeyDown);REQUIRE(hud()==0);
  mode(true);
  auto held=event(13);held.repeat=true;
  send(held, rex::InputEvent::Type::kKeyDown);
  pad=keyboard.GetState(0);REQUIRE(pad);
  CHECK(uint16_t(pad->state.buttons)==0);CHECK(hud()==4306);
  send(enter, rex::InputEvent::Type::kKeyUp);
  send(enter, rex::InputEvent::Type::kKeyDown);REQUIRE(hud()==0);
  CHECK(uint16_t(stroke->virtual_key)==uint16_t(rex::VirtualKey::kXInputPadStart));
  send(enter, rex::InputEvent::Type::kKeyUp);REQUIRE(hud()==0);
  CHECK(xam->EndSystemUi(0x1000));
  });
  mem->SystemHeapFree(out);
}
