#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <thread>
#include <tuple>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/input/flags.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/input/input_driver.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include "../../titles/common/guide_button.h"

extern "C" REX_FUNC(__imp__XamInputGetState);
extern "C" REX_FUNC(__imp__XamInputGetKeystroke);
extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHudEx);
extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHud);
REXCVAR_DECLARE(bool, mnk_mode);

TEST_CASE("Dashboard Guide defaults preserve explicit zero and user mask", "[input-p0]") {
  struct ResetFlags {
    ~ResetFlags() {
      for (auto name : {"guide_button", "guide_chord_ms", "guide_chord_users"})
        rex::cvar::ResetToDefault(name);
    }
  } reset;
  for (auto name : {"guide_chord_ms", "guide_chord_users"}) rex::cvar::ResetToDefault(name);
  x360n::ConfigureGuideButton();
  CHECK(rex::cvar::GetFlagByName("guide_chord_ms") == "150");
  CHECK(rex::cvar::GetFlagByName("guide_chord_users") == "15");
  REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "0"));
  REQUIRE(rex::cvar::SetFlagByName("guide_chord_users", "1"));
  x360n::ConfigureGuideButton();
  CHECK(rex::cvar::GetFlagByName("guide_chord_ms") == "0");
  CHECK(rex::cvar::GetFlagByName("guide_chord_users") == "1");
}

namespace {
class OpeningPad final : public rex::input::InputDriver {
 public:
  OpeningPad() : InputDriver(nullptr, 0) {}
  rex::X_STATUS Setup() override { return 0; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    if (connected) for (unsigned i = 0; i < device_count; ++i)
      out.push_back({rex::input::DeviceId(identity + i), 0, "opening fixture", "", false});
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId, rex::input::X_INPUT_STATE* out) override {
    if (!connected) return 1167;
    *out = {}; out->gamepad.buttons = held;
    if (replace_after_state) { identity += 4; held = replacement_buttons; replace_after_state = false; }
    return 0;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId, uint32_t, rex::input::X_INPUT_CAPABILITIES*) override { return 0; }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId, rex::input::X_INPUT_VIBRATION*) override { return 0; }
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t, rex::input::X_INPUT_KEYSTROKE* out) override {
    if (!connected) return 1167;
    if (uint64_t(id) != identity + event_device) return 4306;
    if (next == keys.size()) return 4306;
    *out = {}; out->virtual_key = keys[next].first; out->flags = keys[next].second;
    ++next; return 0;
  }
  uint16_t held = 0;
  bool connected = true;
  uint64_t identity = 1;
  unsigned device_count = 1;
  unsigned event_device = 0;
  bool replace_after_state = false;
  uint16_t replacement_buttons = 0;
  size_t next = 0;
  std::vector<std::pair<uint16_t, uint16_t>> keys;
};
}

TEST_CASE("System UI opening controls stay with their initiating gesture across reopenings", "[sdk][guide-opening]") {
  using namespace rex::input;
  OpeningPad* pad = nullptr;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  config.input_factory = [&](bool) {
    auto input = std::make_unique<InputSystem>(nullptr);
    auto driver = std::make_unique<OpeningPad>(); pad = driver.get();
    input->AddDriver(std::move(driver));
    input->SetDeviceAssignment(std::make_unique<SlotAssignment>());
    return input;
  };
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  const auto address = rt.memory()->SystemHeapAlloc(sizeof(X_INPUT_KEYSTROKE));
  REQUIRE(address);
  auto* base = rt.memory()->virtual_membase();
  auto* stroke = reinterpret_cast<X_INPUT_KEYSTROKE*>(base + address);
  bool extended = false;
  SECTION("HUD ABI") { extended = false; }
  SECTION("HUD Ex ANY_USER ABI") { extended = true; }
  PPCContext ctx{};
  auto query = [&] {
    ctx.r3.u64 = extended ? 0xFF : 0; ctx.r4.u64 = extended ? 1 : address; ctx.r5.u64 = address;
    if (extended) __imp__XamInputGetKeystrokeHudEx(ctx, base);
    else __imp__XamInputGetKeystrokeHud(ctx, base);
    return ctx.r3.u32;
  };
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x90105000, 0}) == 0);
  for (unsigned cycle = 0; cycle < 3; ++cycle) {
    CAPTURE(cycle, extended);
    pad->held = X_INPUT_GAMEPAD_GUIDE | X_INPUT_GAMEPAD_BACK | X_INPUT_GAMEPAD_START;
    pad->keys.clear(); pad->next = 0;
    REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
    REQUIRE(xam->EnableSystemUiInput(0, true));
    REQUIRE(xam->SetSystemUiVisible(0x1000));
    pad->keys = {{0x5815, 1}, {0x5814, 1}, {0x5800, 1}};
    REQUIRE(query() == 0);
    CHECK(stroke->virtual_key == 0x5800); // New A survives the opening gesture.
    CHECK(pad->next == 3);
    pad->keys.push_back({0x5814, 5}); // A later repeat belongs to the same held START.
    CHECK(query() == 4306);
    pad->held = 0;
    pad->keys.push_back({0x5815, 2}); pad->keys.push_back({0x5814, 2});
    CHECK(query() == 4306);
    pad->keys.push_back({0x5814, 1}); // Fresh START after release is a real UI action.
    REQUIRE(query() == 0); CHECK(stroke->virtual_key == 0x5814);
    // The host also captures a raw Guide closing shortcut before the title
    // regains the queue. Its transitions cannot select a dashboard item.
    pad->held = X_INPUT_GAMEPAD_GUIDE | X_INPUT_GAMEPAD_BACK | X_INPUT_GAMEPAD_START;
    rex::kernel::xam::CaptureSystemUiOpeningInput(0);
    REQUIRE(xam->EndSystemUi(0x1000));
    pad->keys = {{0x5814, 1}, {0x5815, 1}, {0x5814, 2}, {0x5815, 2}, {0x5801, 1}};
    pad->next = 0; pad->held = 0;
    ctx.r3.u64 = 0; ctx.r4.u64 = 1; ctx.r5.u64 = address;
    __imp__XamInputGetKeystroke(ctx, base);
    REQUIRE(ctx.r3.u32 == 0); CHECK(stroke->virtual_key == 0x5801); CHECK(pad->next == 5);
  }
  // Ordinary API requests with no initiating buttons preserve START/BACK.
  pad->held = 0; pad->keys = {{0x5814, 1}, {0x5815, 1}}; pad->next = 0;
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  REQUIRE(xam->EnableSystemUiInput(0, true)); REQUIRE(xam->SetSystemUiVisible(0x1000));
  REQUIRE(query() == 0); CHECK(stroke->virtual_key == 0x5814);
  REQUIRE(query() == 0); CHECK(stroke->virtual_key == 0x5815);
  REQUIRE(xam->EndSystemUi(0x1000));
  rt.memory()->SystemHeapFree(address);
}

TEST_CASE("Buttons owned by the system UI do not reach the title after it closes",
          "[sdk][guide-input]") {
  using namespace rex::input;
  const bool saved_mnk = REXCVAR_GET(mnk_mode);
  const bool saved_guide = REXCVAR_GET(guide_button);
  struct Restore {
    bool mnk, guide;
    ~Restore() { REXCVAR_SET(mnk_mode, mnk); REXCVAR_SET(guide_button, guide); }
  } restore{saved_mnk, saved_guide};
  REXCVAR_SET(mnk_mode, true);
  // The 17559 host enables the Guide button for its own polling.
  REXCVAR_SET(guide_button, true);
  rex::cvar::SetFlagByName("keybind_guide", "G");
  mnk::MnkInputDriver* keyboard = nullptr;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  config.input_factory = [&keyboard](bool) {
    auto input = std::make_unique<InputSystem>(nullptr);
    auto driver = std::make_unique<mnk::MnkInputDriver>(nullptr, 0);
    keyboard = driver.get();
    input->AddDriver(std::move(driver));
    input->SetDeviceAssignment(std::make_unique<SlotAssignment>());
    return input;
  };
  REQUIRE(rt.Setup(std::move(config)) == 0);
  REQUIRE(keyboard);
  auto* memory = rt.memory();
  auto* base = memory->virtual_membase();
  const auto pad = memory->SystemHeapAlloc(sizeof(X_INPUT_STATE));
  const auto stroke = memory->SystemHeapAlloc(sizeof(X_INPUT_KEYSTROKE));
  REQUIRE(pad);
  REQUIRE(stroke);
  auto* state = memory->TranslateVirtual<X_INPUT_STATE*>(pad);
  auto* key = memory->TranslateVirtual<X_INPUT_KEYSTROKE*>(stroke);
  PPCContext ctx{};
  auto title_buttons = [&](uint32_t flags = 1) {
    ctx.r3.u64 = 0; ctx.r4.u64 = flags; ctx.r5.u64 = pad;
    __imp__XamInputGetState(ctx, base);
    REQUIRE(ctx.r3.u32 == 0);
    return uint16_t(state->gamepad.buttons);
  };
  auto title_stroke = [&] {
    ctx.r3.u64 = 0; ctx.r4.u64 = 1; ctx.r5.u64 = stroke;
    __imp__XamInputGetKeystroke(ctx, base);
    return ctx.r3.u32;
  };
  auto press = [&](rex::ui::VirtualKey vk, bool down) {
    rex::ui::KeyEvent event(nullptr, vk, 1, false, false, false, false, false);
    if (down) keyboard->OnKeyDown(event); else keyboard->OnKeyUp(event);
  };

  // The Guide button belongs to the system; only privileged queries see it.
  press(rex::ui::VirtualKey::kG, true);
  CHECK((title_buttons() & X_INPUT_GAMEPAD_GUIDE) == 0);
  CHECK((title_buttons(0x80000000) & X_INPUT_GAMEPAD_GUIDE) != 0);
  press(rex::ui::VirtualKey::kG, false);
  while (title_stroke() == 0) {}  // the Guide button produces no title keystroke

  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x90105000, 0}) == 0);
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  REQUIRE(xam->EnableSystemUiInput(0, true));
  REQUIRE(xam->SetSystemUiVisible(0x1000));
  press(rex::ui::VirtualKey::kBack, true);  // B closes the Guide
  CHECK(title_buttons() == 0);
  ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = stroke;
  __imp__XamInputGetKeystrokeHudEx(ctx, base);
  REQUIRE(ctx.r3.u32 == 0);
  CHECK(key->virtual_key == uint16_t(rex::ui::VirtualKey::kXInputPadB));
  REQUIRE(xam->EndSystemUi(0x1000));

  // Still held after the UI closed: neither state nor keystrokes reach the title.
  CHECK((title_buttons() & X_INPUT_GAMEPAD_B) == 0);
  CHECK(title_stroke() == 4306);
  press(rex::ui::VirtualKey::kBack, false);
  CHECK(title_stroke() == 4306);  // release of a press the UI consumed
  CHECK(title_buttons() == 0);
  // A new press belongs to the title again.
  press(rex::ui::VirtualKey::kBack, true);
  CHECK((title_buttons() & X_INPUT_GAMEPAD_B) != 0);
  REQUIRE(title_stroke() == 0);
  CHECK(key->virtual_key == uint16_t(rex::ui::VirtualKey::kXInputPadB));
  CHECK((uint16_t(key->flags) & X_INPUT_KEYSTROKE_KEYDOWN) != 0);
  press(rex::ui::VirtualKey::kBack, false);
  memory->SystemHeapFree(stroke);
  memory->SystemHeapFree(pad);
}

namespace {
// Fresh backend per test, with no synthetic key-ups to hide leaked ownership.
struct ChordFixture {
  OpeningPad* pad = nullptr;
  rex::Runtime rt{std::filesystem::temp_directory_path()};
  rex::system::object_ref<rex::kernel::xam::XamModule> xam;
  uint32_t address = 0;
  uint8_t* base = nullptr;
  uint32_t user = 0;
  ChordFixture() {
    using namespace rex::input;
    rex::RuntimeConfig config{}; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    config.input_factory = [this](bool) {
      auto input = std::make_unique<InputSystem>(nullptr);
      auto driver = std::make_unique<OpeningPad>(); pad = driver.get();
      input->AddDriver(std::move(driver));
      input->SetDeviceAssignment(std::make_unique<SlotAssignment>());
      return input;
    };
    REQUIRE(rt.Setup(std::move(config)) == 0);
    xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    address = rt.memory()->SystemHeapAlloc(sizeof(X_INPUT_STATE));
    REQUIRE(address);
    base = rt.memory()->virtual_membase();
  }
  ~ChordFixture() {
    rex::cvar::SetFlagByName("guide_chord_ms", "0");
    rex::cvar::SetFlagByName("guide_chord_users", "1");
    rt.memory()->SystemHeapFree(address);
  }
  rex::input::X_INPUT_KEYSTROKE* stroke() { return reinterpret_cast<rex::input::X_INPUT_KEYSTROKE*>(base + address); }
  uint32_t Title() {
    PPCContext ctx{}; ctx.r3.u64 = user; ctx.r4.u64 = 1; ctx.r5.u64 = address;
    __imp__XamInputGetKeystroke(ctx, base);
    return ctx.r3.u32;
  }
  uint32_t Hud() {
    PPCContext ctx{}; ctx.r3.u64 = user; ctx.r4.u64 = address;
    __imp__XamInputGetKeystrokeHud(ctx, base);
    return ctx.r3.u32;
  }
  void Open(uint32_t owner = 0) {
    REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x90105000, 0}) == 0);
    REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, owner));
    REQUIRE(xam->EnableSystemUiInput(owner, true));
    REQUIRE(xam->SetSystemUiVisible(0x1000));
  }
  uint16_t State() {
    PPCContext ctx{}; ctx.r3.u64 = user; ctx.r4.u64 = 1; ctx.r5.u64 = address;
    __imp__XamInputGetState(ctx, base);
    REQUIRE(ctx.r3.u32 == 0);
    return uint16_t(reinterpret_cast<rex::input::X_INPUT_STATE*>(base + address)->gamepad.buttons);
  }
  void Close() {
    REQUIRE(xam->EndSystemUi(0x1000));
    REQUIRE(xam->UnregisterSystemApp(0x1000, 0xFF) == 0);
  }
};
}

TEST_CASE("Pending Guide input is discarded on disconnect and backend shutdown", "[sdk][guide-lifecycle]") {
  using namespace rex::input;
  ChordFixture f;
  REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
  f.pad->held = X_INPUT_GAMEPAD_START;
  f.pad->keys = {{0x5814, 1}};
  REQUIRE(f.Title() == 4306);
  SECTION("disconnect") {
    f.pad->connected = false;
    CHECK(f.Title() == 1167);
    f.pad->connected = true; f.pad->identity++;
    f.pad->held = 0; f.pad->keys = {{0x5800, 1}}; f.pad->next = 0;
    REQUIRE(f.Title() == 0);
    CHECK(f.stroke()->virtual_key == 0x5800);
  }
  SECTION("shutdown and replacement driver") {
    auto* input = static_cast<InputSystem*>(f.rt.input_system());
    input->Shutdown();
    auto replacement = std::make_unique<OpeningPad>();
    f.pad = replacement.get();
    input->AddDriver(std::move(replacement));
    REQUIRE(input->Setup() == 0);
    CHECK(f.Title() == 4306);
  }
}

TEST_CASE("Consecutive runtimes do not inherit pending Guide input", "[sdk][guide-lifecycle]") {
  {
    ChordFixture f;
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
    f.pad->held = rex::input::X_INPUT_GAMEPAD_BACK;
    f.pad->keys = {{0x5815, 1}};
    REQUIRE(f.Title() == 4306);
  }
  ChordFixture f;
  REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
  CHECK(f.Title() == 4306);
}

TEST_CASE("Shoulder buttons held at opening map to their own pad keys", "[sdk][guide-opening]") {
  using namespace rex::input;
  ChordFixture f;
  // VK_PAD_RSHOULDER is 0x5804 and VK_PAD_LSHOULDER 0x5805. A held LB must
  // keep only its own transitions; RB stays a new UI action, and vice versa.
  for (const auto& [held, held_vk, other_vk] : {std::tuple{uint16_t(X_INPUT_GAMEPAD_LEFT_SHOULDER), uint16_t(0x5805), uint16_t(0x5804)},
                                                std::tuple{uint16_t(X_INPUT_GAMEPAD_RIGHT_SHOULDER), uint16_t(0x5804), uint16_t(0x5805)}}) {
    CAPTURE(held_vk);
    f.pad->held = held;
    f.Open();
    f.pad->keys = {{held_vk, 1}, {held_vk, 5}, {other_vk, 1}};
    f.pad->next = 0;
    REQUIRE(f.Hud() == 0);
    CHECK(f.stroke()->virtual_key == other_vk);
    CHECK(f.pad->next == 3);
    f.pad->held = 0;
    f.pad->keys.push_back({held_vk, 2}); f.pad->keys.push_back({other_vk, 2});
    REQUIRE(f.Hud() == 0);
    CHECK(f.stroke()->virtual_key == other_vk);
    CHECK((uint16_t(f.stroke()->flags) & X_INPUT_KEYSTROKE_KEYUP) != 0);
    f.Close();
  }
}

TEST_CASE("Guide window has the same state and event decision on all controller indices", "[sdk][guide-lifecycle]") {
  using namespace rex::input;
  for (uint32_t user = 0; user < 4; ++user) {
    CAPTURE(user);
    ChordFixture f; f.user = user; f.pad->device_count = 4; f.pad->event_device = user;
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_users", std::to_string(1 << user)));
    f.pad->held = X_INPUT_GAMEPAD_BACK;
    f.pad->keys = {{0x5815, 1}};
    CHECK(f.State() == 0);
    CHECK(f.Title() == 4306);
    f.pad->held |= X_INPUT_GAMEPAD_START | X_INPUT_GAMEPAD_GUIDE;
    f.pad->keys.push_back({0x5814, 1});
    CHECK(f.State() == 0);
    CHECK(f.Title() == 4306);
    f.pad->held = 0;
    f.pad->keys.push_back({0x5815, 2}); f.pad->keys.push_back({0x5814, 2});
    f.pad->keys.push_back({0x5800, 1});
    REQUIRE(f.Title() == 0);
    CHECK(f.stroke()->virtual_key == 0x5800);
    CHECK(f.stroke()->user_index == user);
  }
}

TEST_CASE("Unselected controllers and dedicated Guide leave ordinary commands immediate", "[sdk][guide-lifecycle]") {
  using namespace rex::input;
  ChordFixture f; f.pad->device_count = 4; f.user = 3; f.pad->event_device = 3;
  REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
  REQUIRE(rex::cvar::SetFlagByName("guide_chord_users", "1"));
  f.pad->held = X_INPUT_GAMEPAD_START;
  f.pad->keys = {{0x5814, 1}, {0x5814, 5}, {0x5814, 2}};
  CHECK(f.State() == X_INPUT_GAMEPAD_START);
  for (uint16_t flags : {1, 5, 2}) { REQUIRE(f.Title() == 0); CHECK(f.stroke()->flags == flags); }
  f.pad->held = X_INPUT_GAMEPAD_GUIDE | X_INPUT_GAMEPAD_A;
  f.pad->keys.push_back({0x5800, 1});
  CHECK(f.State() == X_INPUT_GAMEPAD_A);
  REQUIRE(f.Title() == 0); CHECK(f.stroke()->virtual_key == 0x5800);
}

TEST_CASE("Disabling a pending Guide window preserves DOWN repeat UP and queue order", "[sdk][guide-lifecycle]") {
  ChordFixture f;
  REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
  f.pad->held = rex::input::X_INPUT_GAMEPAD_START;
  f.pad->keys = {{0x5814, 1}, {0x5814, 5}, {0x5800, 1}, {0x5814, 2}};
  REQUIRE(f.Title() == 4306);
  REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "0"));
  for (auto [key, flags] : f.pad->keys) {
    REQUIRE(f.Title() == 0);
    CHECK(f.stroke()->virtual_key == key); CHECK(f.stroke()->flags == flags);
  }
  CHECK(f.Title() == 4306);
}

TEST_CASE("ANY_USER queries reach each controller without stealing a system app owner", "[sdk][guide-lifecycle]") {
  for (unsigned index = 0; index < 4; ++index) {
    CAPTURE(index);
    ChordFixture f; f.pad->device_count = 4; f.pad->event_device = index;
    f.user = 0xFF; f.pad->keys = {{0x5800, 1}};
    REQUIRE(f.Title() == 0); CHECK(f.stroke()->user_index == index);
    f.Open();
    f.pad->keys.push_back({0x5801, 1});
    PPCContext ctx{}; ctx.r3.u64 = 0xFF; ctx.r4.u64 = 1; ctx.r5.u64 = f.address;
    __imp__XamInputGetKeystrokeHudEx(ctx, f.base);
    // Open() assigned user 0; ANY_USER must not change that ownership.
    CHECK(ctx.r3.u32 == (index == 0 ? 0 : 4306));
    if (index != 0) { REQUIRE(f.Title() == 0); CHECK(f.stroke()->user_index == index); }
    f.Close();
  }
}

TEST_CASE("A BACK+START Guide chord never reaches the title or the HUD", "[sdk][guide-chord]") {
  using namespace rex::input;
  ChordFixture f;
  constexpr uint16_t kStart = 0x5814, kBack = 0x5815, kA = 0x5800;
  constexpr uint16_t kChord = X_INPUT_GAMEPAD_START | X_INPUT_GAMEPAD_BACK | X_INPUT_GAMEPAD_GUIDE;

  SECTION("disabled: START is delivered immediately") {
    f.pad->held = X_INPUT_GAMEPAD_START;
    f.pad->keys = {{kStart, 1}};
    REQUIRE(f.Title() == 0);
    CHECK(f.stroke()->virtual_key == kStart);
    f.pad->held = 0; f.pad->keys.push_back({kStart, 2});
    REQUIRE(f.Title() == 0);
  }
  SECTION("title: first button waits, the chord is consumed") {
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
    f.pad->held = X_INPUT_GAMEPAD_START;
    f.pad->keys = {{kStart, 1}};
    CHECK(f.Title() == 4306);  // pending: the partner may still arrive
    CHECK(f.pad->next == 1);
    f.pad->held = kChord;
    f.pad->keys.push_back({kBack, 1});
    CHECK(f.Title() == 4306);  // GUIDE seen: START belongs to the system UI
    CHECK(f.Title() == 4306);  // the second button's key-down too
    f.pad->keys.push_back({kStart, 5});
    CHECK(f.Title() == 4306);  // and its repeats
    f.pad->held = 0;
    f.pad->keys.push_back({kStart, 2}); f.pad->keys.push_back({kBack, 2});
    f.pad->keys.push_back({kA, 1});
    REQUIRE(f.Title() == 0);
    CHECK(f.stroke()->virtual_key == kA);
    CHECK(f.pad->next == f.pad->keys.size());
    // Released: a fresh START is an ordinary title action again.
    f.pad->keys.push_back({kA, 2});
    REQUIRE(f.Title() == 0);
    f.pad->held = X_INPUT_GAMEPAD_START;
    f.pad->keys.push_back({kStart, 1});
    CHECK(f.Title() == 4306);
    f.pad->held = 0;
    f.pad->keys.push_back({kStart, 2});
    REQUIRE(f.Title() == 0);  // released inside the window: delivered at once
    CHECK(f.stroke()->virtual_key == kStart);
    CHECK((uint16_t(f.stroke()->flags) & X_INPUT_KEYSTROKE_KEYDOWN) != 0);
    REQUIRE(f.Title() == 0);
    CHECK((uint16_t(f.stroke()->flags) & X_INPUT_KEYSTROKE_KEYUP) != 0);
  }
  SECTION("title: a held START without GUIDE is delivered after the window") {
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "40"));
    f.pad->held = X_INPUT_GAMEPAD_START;
    f.pad->keys = {{kStart, 1}, {kA, 1}};
    CHECK(f.Title() == 4306);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    REQUIRE(f.Title() == 0);
    CHECK(f.stroke()->virtual_key == kStart);
    REQUIRE(f.Title() == 0);  // later keys kept their order behind it
    CHECK(f.stroke()->virtual_key == kA);
    f.pad->held = 0;
    f.pad->keys.push_back({kStart, 2}); f.pad->keys.push_back({kA, 2});
    REQUIRE(f.Title() == 0); REQUIRE(f.Title() == 0);
  }
  SECTION("HUD: the closing chord is not a UI action") {
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
    f.Open();
    f.pad->held = X_INPUT_GAMEPAD_BACK;
    f.pad->keys = {{kBack, 1}};
    CHECK(f.Hud() == 4306);
    f.pad->held = kChord;
    f.pad->keys.push_back({kStart, 1});
    CHECK(f.Hud() == 4306);
    CHECK(f.Hud() == 4306);
    f.pad->held = 0;
    f.pad->keys.push_back({kBack, 2}); f.pad->keys.push_back({kStart, 2});
    f.pad->keys.push_back({kA, 1});
    REQUIRE(f.Hud() == 0);
    CHECK(f.stroke()->virtual_key == kA);
    f.pad->keys.push_back({kA, 2});
    REQUIRE(f.Hud() == 0);
    f.Close();
  }
}

TEST_CASE("Opening a queued Guide preserves the initiating chord after its buttons are released", "[sdk][guide-chord][input-p0]") {
  using namespace rex::input;
  for (uint32_t user = 0; user < 4; ++user) {
    ChordFixture f; f.user = user; f.pad->device_count = 4; f.pad->event_device = user;
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_users", "15"));
    f.pad->held = X_INPUT_GAMEPAD_START;
    f.pad->keys = {{0x5814, 1}};
    CHECK(f.Title() == 4306);
    f.pad->held |= X_INPUT_GAMEPAD_BACK | X_INPUT_GAMEPAD_GUIDE;
    rex::kernel::xam::CaptureSystemUiOpeningInput(user);
    // Native app loading takes longer than the physical gesture.
    f.pad->held = 0;
    f.pad->keys.insert(f.pad->keys.end(), {{0x5815, 1}, {0x5814, 2}, {0x5815, 2}, {0x5800, 1}});
    REQUIRE(f.xam->RegisterSystemApp({0x1000, 0xFF, 0x90105000, 0}) == 0);
    REQUIRE(f.xam->BeginSystemUi(0x1000, 0xFF, user));
    REQUIRE(f.xam->EnableSystemUiInput(user, true));
    REQUIRE(f.xam->SetSystemUiVisible(0x1000));
    REQUIRE(f.Hud() == 0);
    CHECK(f.stroke()->virtual_key == 0x5800);
    CHECK(f.pad->next == f.pad->keys.size());
    f.Close();
  }
}

TEST_CASE("A device replaced between state and keystroke cannot bypass Guide ownership", "[sdk][guide-lifecycle][input-p0]") {
  using namespace rex::input;
  for (uint32_t user = 0; user < 4; ++user) {
    for (bool hud : {false, true}) {
      CAPTURE(user, hud);
      ChordFixture f; f.user = user; f.pad->device_count = 4; f.pad->event_device = user;
      REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
      REQUIRE(rex::cvar::SetFlagByName("guide_chord_users", "15"));
      if (hud) f.Open(user);
      const auto query = [&] { return hud ? f.Hud() : f.Title(); };
      f.pad->held = 0;
      f.pad->replacement_buttons = X_INPUT_GAMEPAD_GUIDE | X_INPUT_GAMEPAD_BACK | X_INPUT_GAMEPAD_START;
      f.pad->replace_after_state = true;
      f.pad->keys = {{0x5814, 1}, {0x5815, 1}};
      CHECK(query() == 4306);
      CHECK(query() == 4306);
      f.pad->held = 0;
      f.pad->keys.insert(f.pad->keys.end(), {{0x5814, 2}, {0x5815, 2}, {0x5800, 1}});
      REQUIRE(query() == 0);
      CHECK(f.stroke()->virtual_key == 0x5800);
      if (hud) f.Close();
    }
  }
}

TEST_CASE("Guide edges belong to a connection and do not swallow new controls while held", "[sdk][guide-lifecycle][input-p0]") {
  using namespace rex::input;
  for (uint32_t user = 0; user < 4; ++user) {
    CAPTURE(user);
    ChordFixture f; f.user = user; f.pad->device_count = 4; f.pad->event_device = user;
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_ms", "150"));
    REQUIRE(rex::cvar::SetFlagByName("guide_chord_users", "15"));
    const auto poll = [&] { return rex::kernel::xam::PollGuideButton(user); };
    CHECK_FALSE(poll()); // connected and neutral
    f.pad->held = X_INPUT_GAMEPAD_START; f.pad->keys = {{0x5814, 1}};
    CHECK(f.Title() == 4306);
    f.pad->connected = false;
    CHECK(f.Title() == 1167);
    f.pad->connected = true; f.pad->identity += 4;
    f.pad->held = 0; f.pad->keys.clear(); f.pad->next = 0;
    CHECK(f.Title() == 4306); CHECK_FALSE(poll());
    for (bool dedicated : {false, true}) {
      CAPTURE(dedicated);
      const uint16_t controls = dedicated ? 0 : X_INPUT_GAMEPAD_START | X_INPUT_GAMEPAD_BACK;
      f.pad->held = X_INPUT_GAMEPAD_GUIDE | controls;
      f.pad->keys.clear(); f.pad->next = 0;
      if (!dedicated) f.pad->keys = {{0x5814, 1}, {0x5815, 1}};
      REQUIRE(poll());
      REQUIRE(f.xam->RegisterSystemApp({0x1000, 0xFF, 0x90105000, 0}) == 0);
      REQUIRE(f.xam->BeginSystemUi(0x1000, 0xFF, user));
      REQUIRE(f.xam->EnableSystemUiInput(user, true));
      REQUIRE(f.xam->SetSystemUiVisible(0x1000));
      // Holding GUIDE must not steal a subsequent, deliberate A.
      f.pad->held |= X_INPUT_GAMEPAD_A;
      f.pad->keys.push_back({0x5800, 1});
      CHECK_FALSE(poll());
      REQUIRE(f.Hud() == 0); CHECK(f.stroke()->virtual_key == 0x5800);
      f.pad->held = 0; CHECK_FALSE(poll());
      if (!dedicated) { f.pad->keys.push_back({0x5814, 2}); f.pad->keys.push_back({0x5815, 2}); }
      f.pad->keys.push_back({0x5800, 2});
      REQUIRE(f.Hud() == 0); CHECK(f.stroke()->virtual_key == 0x5800);
      // Disconnect with a command waiting in the chord window.
      f.pad->held = X_INPUT_GAMEPAD_BACK; f.pad->keys.push_back({0x5815, 1});
      CHECK(f.Hud() == 4306);
      f.pad->connected = false;
      CHECK_FALSE(poll()); CHECK(f.Hud() == 1167);
      f.pad->connected = true; f.pad->identity += 4;
      f.pad->held = X_INPUT_GAMEPAD_GUIDE | controls;
      f.pad->keys = dedicated ? std::vector<std::pair<uint16_t,uint16_t>>{} :
          std::vector<std::pair<uint16_t,uint16_t>>{{0x5814, 1}, {0x5815, 1}};
      f.pad->next = 0;
      CHECK_FALSE(poll()); // held while connecting is not a new Guide press
      CHECK(f.Hud() == 4306);
      f.pad->held = 0; CHECK_FALSE(poll());
      if (!dedicated) { f.pad->keys.push_back({0x5814, 2}); f.pad->keys.push_back({0x5815, 2}); }
      CHECK(f.Hud() == 4306);
      f.pad->held = X_INPUT_GAMEPAD_GUIDE;
      REQUIRE(poll()); // released then pressed: close exactly once
      f.Close();
      CHECK_FALSE(poll());
      f.pad->held = 0; CHECK_FALSE(poll()); CHECK(f.State() == 0);
      f.pad->keys.push_back({0x5800, 1});
      REQUIRE(f.Title() == 0); CHECK(f.stroke()->virtual_key == 0x5800);
      f.pad->keys.push_back({0x5800, 2}); REQUIRE(f.Title() == 0);
    }
  }
}

TEST_CASE("Released opening controls without queued UP do not poison the next press", "[sdk][guide-opening][input-p0]") {
  using namespace rex::input;
  ChordFixture f;
  f.pad->held = X_INPUT_GAMEPAD_GUIDE | X_INPUT_GAMEPAD_START | X_INPUT_GAMEPAD_BACK;
  f.Open();
  // SDL generates transitions on demand; a gesture released before the first
  // query has neither a DOWN nor an UP in its queue.
  f.pad->held = 0;
  REQUIRE(f.Hud() == 4306);
  for (auto [button, key] : {std::pair{X_INPUT_GAMEPAD_START, 0x5814},
                           std::pair{X_INPUT_GAMEPAD_BACK, 0x5815},
                           std::pair{X_INPUT_GAMEPAD_A, 0x5800}}) {
    f.pad->held = button; f.pad->keys.push_back({uint16_t(key), 1});
    REQUIRE(f.Hud() == 0); CHECK(f.stroke()->virtual_key == key);
    f.pad->held = 0; f.pad->keys.push_back({uint16_t(key), 2});
    REQUIRE(f.Hud() == 0); CHECK(f.stroke()->virtual_key == key);
  }
  f.Close();
  // No title poll between closing and this new physical press.
  f.pad->held = X_INPUT_GAMEPAD_A; f.pad->keys.push_back({0x5800, 1});
  CHECK(f.State() == X_INPUT_GAMEPAD_A);
  REQUIRE(f.Title() == 0); CHECK(f.stroke()->virtual_key == 0x5800);
}
