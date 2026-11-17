#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <rex/xam/input.h>
#include <rex/xam/system_ui.h>
#include "hid/input_harness.h"
#include "kernel/kernel_fixture.h"
#include "xam/xam_state.h"
#include "../../titles/common/guide_button.h"

TEST_CASE("Original Guide defaults preserve explicit user input settings",
          "[sdk][guide-input-settings]") {
  rex::test::KernelFixture fixture;
  auto& settings = fixture.GetInput().GetSettings();
  auto reset = [&] {
    REQUIRE(settings.guide_button.Reset());
    REQUIRE(settings.guide_chord_ms.Reset());
    REQUIRE(settings.guide_chord_users.Reset());
  };
  reset();
  rex::ScopeGuard restore(reset);
  x360n::ConfigureGuideButton();
  CHECK(settings.guide_button.Get());
  CHECK(settings.guide_chord_ms.Get() == 150);
  CHECK(settings.guide_chord_users.Get() == 15);
  REQUIRE(settings.guide_button.SetText(rex::SettingLayer::kCommandLine, "false"));
  REQUIRE(settings.guide_chord_ms.SetText(rex::SettingLayer::kCommandLine, "0"));
  REQUIRE(settings.guide_chord_users.SetText(rex::SettingLayer::kCommandLine, "2"));
  x360n::ConfigureGuideButton();
  CHECK_FALSE(settings.guide_button.Get());
  CHECK(settings.guide_chord_ms.Get() == 0);
  CHECK(settings.guide_chord_users.Get() == 2);
}

namespace {
using namespace rex::test_input;
struct InputFixture {
  InputFixture() {
    g_fake = {};
    g_fake[0].devices = {MakePad(1, 0), MakePad(2, 1)};
    g_fake[0].states[1] = Buttons(0); g_fake[0].states[2] = Buttons(0);
    fixture = std::make_unique<rex::test::KernelFixture>(std::function<void(const std::filesystem::path&)>{},
        std::vector<rex::IModule*>{&provider}, "", "", "fake_a");
    REQUIRE(fixture->GetInput().GetSettings().guide_button.SetText(rex::SettingLayer::kTitle, "true"));
    rex::xam::CurrentXAM().AttachInput(&fixture->GetInput());
  }
  ~InputFixture() { rex::xam::CurrentXAM().DetachInput(); }
  void ButtonsFor(rex::u16 buttons, rex::u32 user = 0) { g_fake[0].states[user + 1] = Buttons(buttons); }
  void Begin() {
    auto& ui = rex::xam::CurrentXAM().GetSystemUI();
    REQUIRE(ui.RegisterSystemApp({0x1000, 0xFF, 0x82000000, 0}) == 0);
    REQUIRE(ui.BeginSystemUi(0x1000, 0xFF, 0));
    REQUIRE(ui.SetSystemUiVisible(0x1000)); REQUIRE(ui.EnableSystemUiInput(0, true));
  }
  rex::test_input::FakeProvider provider;
  std::unique_ptr<rex::test::KernelFixture> fixture;
};
}

TEST_CASE("Guide opening and closing preserve held-input ownership without consuming fresh controls", "[sdk][xam][system-ui-input]") {
  InputFixture input;
  input.ButtonsFor(rex::hid::kPadA);
  input.Begin();
  input.fixture->RunOnGuestThread([&](rex::test::Caller& c) {
    const auto state = c.Alloc(16), stroke = c.Alloc(8);
    CHECK(c.XAM("XamInputGetState", {0, 1, state}) == 0);
    CHECK(c.Load32(state + 4) == 0);
    CHECK(c.XAM("XamInputGetState", {0, 0x80000001, state}) == 0);
    CHECK((c.Load32(state + 4) >> 16) == rex::hid::kPadA);
    CHECK(c.XAM("XamInputGetKeystrokeHud", {0, stroke}) == 4306);
    input.ButtonsFor(rex::hid::kPadA | rex::hid::kPadB);
    REQUIRE(c.XAM("XamInputGetKeystrokeHud", {0, stroke}) == 0);
    CHECK((c.Load32(stroke) >> 16) == 0x5801);
    REQUIRE(rex::xam::CurrentXAM().GetSystemUI().EndSystemUi(0x1000));
    REQUIRE(c.XAM("XamInputGetState", {0, 1, state}) == 0);
    CHECK(c.Load32(state + 4) == 0);
    input.ButtonsFor(0);
    CHECK(c.XAM("XamInputGetKeystroke", {0, 1, stroke}) == 4306);
    input.ButtonsFor(rex::hid::kPadX);
    REQUIRE(c.XAM("XamInputGetState", {0, 1, state}) == 0);
    CHECK((c.Load32(state + 4) >> 16) == rex::hid::kPadX);
    REQUIRE(c.XAM("XamInputGetKeystroke", {0, 1, stroke}) == 0);
    CHECK((c.Load32(stroke) >> 16) == 0x5802);
  });
}

TEST_CASE("A short BACK tap delayed by the optional chord window remains a distinct state packet", "[sdk][xam][system-ui-input]") {
  InputFixture input;
  REQUIRE(input.fixture->GetInput().GetSettings().guide_chord_ms.SetText(rex::SettingLayer::kTitle, "1000"));
  input.fixture->RunOnGuestThread([&](rex::test::Caller& c) {
    const auto state = c.Alloc(16);
    input.ButtonsFor(rex::hid::kPadBack);
    REQUIRE(c.XAM("XamInputGetState", {0, 1, state}) == 0);
    CHECK(c.Load32(state + 4) == 0);
    input.ButtonsFor(0);
    REQUIRE(c.XAM("XamInputGetState", {0, 1, state}) == 0);
    CHECK((c.Load32(state + 4) >> 16) == rex::hid::kPadBack);
    const auto down = c.Load32(state);
    REQUIRE(c.XAM("XamInputGetState", {0, 1, state}) == 0);
    CHECK(c.Load32(state + 4) == 0); CHECK(c.Load32(state) != down);
  });
}

TEST_CASE("Guide consumes a pending BACK START gesture and requires release after reconnection", "[sdk][xam][system-ui-input]") {
  InputFixture input;
  REQUIRE(input.fixture->GetInput().GetSettings().guide_chord_ms.SetText(rex::SettingLayer::kTitle, "1000"));
  CHECK_FALSE(rex::xam::PollGuideButton(0));
  input.fixture->RunOnGuestThread([&](rex::test::Caller& c) {
    const auto state = c.Alloc(16), stroke = c.Alloc(8);
    input.ButtonsFor(rex::hid::kPadBack);
    CHECK(c.XAM("XamInputGetKeystroke", {0, 1, stroke}) == 4306);
    input.ButtonsFor(rex::hid::kPadBack | rex::hid::kPadStart | rex::hid::kPadGuide);
    CHECK(rex::xam::PollGuideButton(0)); CHECK_FALSE(rex::xam::PollGuideButton(0));
    REQUIRE(c.XAM("XamInputGetState", {0, 1, state}) == 0);
    CHECK(c.Load32(state + 4) == 0);
    input.ButtonsFor(0);
    CHECK_FALSE(rex::xam::PollGuideButton(0));
    CHECK(c.XAM("XamInputGetKeystroke", {0, 1, stroke}) == 4306);
    g_fake[0].devices[0] = MakePad(3, 0);
    g_fake[0].states.erase(1);
    g_fake[0].states[3] = Buttons(rex::hid::kPadGuide | rex::hid::kPadA);
    ++g_fake[0].revision;
    CHECK_FALSE(rex::xam::PollGuideButton(0));
    REQUIRE(c.XAM("XamInputGetState", {0, 1, state}) == 0);
    CHECK(c.Load32(state + 4) == 0);
    g_fake[0].states[3] = Buttons(0);
    CHECK_FALSE(rex::xam::PollGuideButton(0));
    REQUIRE(c.XAM("XamInputGetState", {0, 1, state}) == 0);
    g_fake[0].states[3] = Buttons(rex::hid::kPadGuide);
    CHECK(rex::xam::PollGuideButton(0));
  });
}

TEST_CASE("Any-user input queries preserve events from a controller outside the original UI ownership", "[sdk][xam][system-ui-input]") {
  InputFixture input;
  input.Begin();
  input.ButtonsFor(rex::hid::kPadStart, 1);
  input.fixture->RunOnGuestThread([&](rex::test::Caller& c) {
    const auto user = c.Alloc(4), stroke = c.Alloc(8);
    c.Store32(user, 0);
    REQUIRE(c.XAM("XamInputGetKeystrokeEx", {user, 0x40000001, stroke}) == 0);
    CHECK(c.Load32(user) == 1);
    CHECK((c.Load32(stroke) >> 16) == 0x5814);
    CHECK(c.Load8(stroke + 6) == 1);
  });
  REQUIRE(rex::xam::CurrentXAM().GetSystemUI().EndSystemUi(0x1000));
}

#include "../../titles/dash_1888/src/guide_diagnostic.h"

TEST_CASE("1888 diagnostic pad uses the canonical driver selection and original cycle boundaries",
          "[sdk][guide-input-settings]") {
  dash1888::GuideDiagnosticProvider provider;
  rex::test::KernelFixture fixture({}, {&provider}, {}, {}, "dash1888-probe");
  auto& input = fixture.GetInput();
  CHECK(input.GetSettings().drivers.Get() == "dash1888-probe");
  REQUIRE(input.GetSettings().guide_button.SetText(rex::SettingLayer::kRuntime, "true"));
  const auto capabilities = input.GetCapabilities(0);
  REQUIRE(capabilities);
  CHECK(capabilities->supported.buttons == 0xFFFF);
  const auto reading = input.GetState(0);
  REQUIRE(reading);
  CHECK(reading->state.buttons == 0); // The scripted diagnostic has a three-second delay.
  using dash1888::GuideDiagnostic;
  for (const auto cycle : {0, 6000, 12000}) {
    CHECK(GuideDiagnostic::StateAt(cycle - (cycle == 0 ? 1 : 0), false).buttons ==
          (cycle == 0 ? 0 : (rex::hid::kPadGuide | rex::hid::kPadBack | rex::hid::kPadStart)));
    CHECK(GuideDiagnostic::StateAt(cycle + 221, false).buttons & rex::hid::kPadGuide);
    CHECK(GuideDiagnostic::StateAt(cycle + 222, false).buttons == 0);
    CHECK(GuideDiagnostic::StateAt(cycle + 3000, false).buttons & rex::hid::kPadGuide);
    CHECK(GuideDiagnostic::StateAt(cycle + 3000, true).buttons == rex::hid::kPadB);
    CHECK(GuideDiagnostic::StateAt(cycle + 3222, true).buttons == 0);
  }
  CHECK(GuideDiagnostic::StateAt(18000, false).buttons == 0);
}
