#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <filesystem>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

extern "C" REX_FUNC(__imp__XamInputSetKeyboardTranslationHud);
extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHudEx);
extern "C" REX_FUNC(__imp__XamInputGetKeystroke);
REXCVAR_DECLARE(bool, mnk_mode);
REXCVAR_DECLARE(bool, mnk_mouse);

namespace {
class KeyboardContext final : public rex::ui::WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};
// Exercise Window's real listener ordering and deferred callbacks without an OS
// window, renderer, or synthetic substitute for the original keyboard scene.
class KeyboardWindow final : public rex::ui::Window {
 public:
  explicit KeyboardWindow(KeyboardContext& context) : Window(context,"keyboard input test",640,480) {}
  void Character(uint16_t value) {
    rex::ui::KeyEvent event(this,rex::ui::VirtualKey(value),1,false,false,false,false,false);
    WindowDestructionReceiver receiver(this);OnKeyChar(event,receiver);
  }
 private:
  bool OpenImpl() override { return true; }
  void RequestCloseImpl() override {
    WindowDestructionReceiver receiver(this);OnBeforeClose(receiver);OnAfterClose();
  }
  std::unique_ptr<rex::ui::Surface> CreateSurfaceImpl(rex::ui::Surface::TypeFlags) override {return {};}
  void RequestPaintImpl() override {}
};
class OverlayTextListener final : public rex::ui::WindowInputListener {
 public:
  void OnKeyChar(rex::ui::KeyEvent& event) override {++characters;event.set_handled(true);}
  unsigned characters=0;
};
}

TEST_CASE("Original keyboard owns window text ahead of overlays and survives mouse release",
          "[sdk][keyboard-text-input]") {
  const bool old_mode=REXCVAR_GET(mnk_mode),old_mouse=REXCVAR_GET(mnk_mouse);
  struct Restore {bool mode,mouse;~Restore(){REXCVAR_SET(mnk_mode,mode);REXCVAR_SET(mnk_mouse,mouse);}} restore{old_mode,old_mouse};
  REXCVAR_SET(mnk_mode,true);REXCVAR_SET(mnk_mouse,true);
  KeyboardContext context;KeyboardWindow window(context);REQUIRE(window.Open());
  OverlayTextListener overlay;window.AddInputListener(&overlay,64);
  {
    rex::input::mnk::MnkInputDriver keyboard(nullptr,0);keyboard.OnWindowAvailable(&window);
    std::vector<rex::input::DeviceInfo> devices;keyboard.EnumerateDevices(devices);REQUIRE(devices.size()==1);
    rex::input::X_INPUT_STATE pad{};REQUIRE(keyboard.GetDeviceState(devices[0].id,&pad)==0);
    context.ExecutePendingFunctionsFromUIThread();
    keyboard.SetKeyboardTranslation(false);
    REQUIRE(keyboard.GetDeviceState(devices[0].id,&pad)==0);
    context.ExecutePendingFunctionsFromUIThread();
    CHECK(window.IsTextInputActive());
    window.Character(0xE7);
    rex::input::X_INPUT_KEYSTROKE stroke{};
    REQUIRE(keyboard.GetDeviceKeystroke(devices[0].id,0,&stroke)==0);
    CHECK(uint16_t(stroke.unicode)==0xE7);CHECK(overlay.characters==0);
    keyboard.SetKeyboardTranslation(true);context.ExecutePendingFunctionsFromUIThread();
    CHECK_FALSE(window.IsTextInputActive());
    window.Character('a');CHECK(overlay.characters==1);
    CHECK(keyboard.GetDeviceKeystroke(devices[0].id,0,&stroke)==4306);
  }
  window.RemoveInputListener(&overlay);window.RequestClose();
}

TEST_CASE("Original keyboard text mode accepts UTF16 without controller shortcuts or title leakage",
          "[sdk][keyboard-text-input]") {
  const bool previous=REXCVAR_GET(mnk_mode);
  struct Restore { bool value; ~Restore(){REXCVAR_SET(mnk_mode,value);} } restore{previous};
  REXCVAR_SET(mnk_mode,true);
  rex::input::mnk::MnkInputDriver* keyboard=nullptr;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
  config.input_factory=[&](bool) {
    auto input=std::make_unique<rex::input::InputSystem>(nullptr);
    auto driver=std::make_unique<rex::input::mnk::MnkInputDriver>(nullptr,0);keyboard=driver.get();
    input->AddDriver(std::move(driver));input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
    return input;
  };
  REQUIRE(rt.Setup(std::move(config))==0);
  auto xam=rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam->RegisterSystemApp({0x1000,0xFF,0x917DE3C8,11})==0);
  REQUIRE(xam->BeginSystemUi(0x1000,0xFF,0));
  REQUIRE(xam->EnableSystemUiInput(0,true));REQUIRE(xam->SetSystemUiVisible(0x1000));
  auto* mem=rt.memory();auto* base=mem->virtual_membase();const auto out=mem->SystemHeapAlloc(64);REQUIRE(out);
  auto* stroke=mem->TranslateVirtual<rex::input::X_INPUT_KEYSTROKE*>(out);
  const auto event=[&](uint16_t code) { return rex::ui::KeyEvent(nullptr,rex::ui::VirtualKey(code),1,false,false,false,false,false); };
  const auto hud=[&] {PPCContext ctx{};ctx.r3.u64=0;ctx.r4.u64=0x20000000;ctx.r5.u64=out;
    __imp__XamInputGetKeystrokeHudEx(ctx,base);return ctx.r3.u32;};
  const auto mode=[&](bool translate) {PPCContext ctx{};ctx.r3.u64=translate;
    __imp__XamInputSetKeyboardTranslationHud(ctx,base);};
  mode(false); // Original vk sends 0 on focus, 1 when leaving/destroying.
  for(const auto value : {uint16_t(' '),uint16_t(0xE7),uint16_t(0xD83D),uint16_t(0xDE00)}) {
    auto key=event(value);keyboard->OnKeyChar(key);
    PPCContext game{};game.r3.u64=0;game.r4.u64=1;game.r5.u64=out;
    __imp__XamInputGetKeystroke(game,base);CHECK(game.r3.u32==4306);
    REQUIRE(hud()==0);CHECK(uint16_t(stroke->unicode)==value);
    CHECK((uint16_t(stroke->flags)&1)!=0);CHECK(stroke->user_index==0);
  }
  auto back=event(8);keyboard->OnKeyDown(back);
  REQUIRE(hud()==0);CHECK(uint16_t(stroke->virtual_key)==8);CHECK(uint16_t(stroke->unicode)==0);
  keyboard->OnKeyUp(back);REQUIRE(hud()==0);CHECK((uint16_t(stroke->flags)&2)!=0);
  auto space=event(32);keyboard->OnKeyDown(space);
  rex::input::X_INPUT_STATE pad{};REQUIRE(static_cast<rex::input::InputSystem*>(rt.input_system())->GetState(0,&pad)==0);
  CHECK(uint16_t(pad.gamepad.buttons)==0);keyboard->OnKeyUp(space);
  auto stale=event('x');keyboard->OnKeyChar(stale);mode(true);CHECK(hud()==4306);
  keyboard->OnKeyDown(space);REQUIRE(hud()==0);
  CHECK(uint16_t(stroke->virtual_key)==uint16_t(rex::ui::VirtualKey::kXInputPadA));
  keyboard->OnKeyUp(space);REQUIRE(hud()==0);
  // Confirm held across UI closure must not become a game START repeat.
  mode(false);auto enter=event(13);keyboard->OnKeyDown(enter);REQUIRE(hud()==0);
  mode(true);
  rex::ui::KeyEvent held(nullptr,rex::ui::VirtualKey(13),1,true,false,false,false,false);
  keyboard->OnKeyDown(held);
  REQUIRE(static_cast<rex::input::InputSystem*>(rt.input_system())->GetState(0,&pad)==0);
  CHECK(uint16_t(pad.gamepad.buttons)==0);CHECK(hud()==4306);
  keyboard->OnKeyUp(enter);
  keyboard->OnKeyDown(enter);REQUIRE(hud()==0);
  CHECK(uint16_t(stroke->virtual_key)==uint16_t(rex::ui::VirtualKey::kXInputPadStart));
  keyboard->OnKeyUp(enter);REQUIRE(hud()==0);
  CHECK(xam->EndSystemUi(0x1000));mem->SystemHeapFree(out);
}
