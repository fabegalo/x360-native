#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/input/flags.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

extern "C" REX_FUNC(__imp__XamInputGetState);
extern "C" REX_FUNC(__imp__XamInputGetKeystroke);
extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHudEx);
REXCVAR_DECLARE(bool, mnk_mode);

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
