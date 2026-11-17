#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include "kernel/title_state.h"
#include "kernel/module_registry.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/module.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/notify_listener_object.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/types/io.h>
#include <rex/chrono.h>
#include <rex/hid/input_system.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/xam/system_ui.h>
#include <rex/xam/title_lifecycle.h>
#include "xam/xam_state.h"
#include "../../titles/dash_17559/src/system_ui_request.h"
#include "../../titles/dash_17559/src/system_ui.h"

extern "C" REX_FUNC(__imp__XamShowSigninUI);
extern "C" REX_FUNC(__imp__XamShowGuideUI);
extern "C" REX_FUNC(__imp__XamShowSigninUIp);
extern "C" REX_FUNC(__imp__XamHudGetUserIndex);
extern "C" REX_FUNC(__imp__XamShowGamerCardUI);
extern "C" REX_FUNC(__imp__XamShowCreateProfileUI);
extern "C" REX_FUNC(__imp__XamShowCreateProfileUIEx);
extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHud);
extern "C" REX_FUNC(__imp__XamAppUnloadSelf);
extern "C" REX_FUNC(__imp__XamEnableSystemAppInput);
extern "C" REX_FUNC(__imp__XamInputGetKeystroke);
extern "C" REX_FUNC(__imp__XamIsUIActive);
extern "C" REX_FUNC(__imp__XamAppUnloadStack);
extern "C" REX_FUNC(__imp__XamInputGetState);
extern "C" REX_FUNC(__imp__GetTickCount);
extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHudEx);
extern "C" REX_FUNC(__imp__XamAppLoad);
extern "C" REX_FUNC(__imp__XamAppRequestLoad);
using namespace rex::kernel;

namespace {
void Call(rex::test::KernelFixture& fixture,PPCContext& args,uint8_t* base,
          void (*function)(PPCContext&,uint8_t*)) {
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& context=caller.GetContext(); const auto stack=args.r1.u64;
    if(stack)context.r1.u64=stack;
    context.r3=args.r3;context.r4=args.r4;context.r5=args.r5;context.r6=args.r6;
    context.r7=args.r7;context.r8=args.r8;context.r9=args.r9;context.r10=args.r10;
    function(context,base);args=context;args.r1.u64=stack;
  });
}
}
TEST_CASE("Sign-in routes to its original application with the original user field", "[sdk][signin]") {
  using namespace dash17559;
  const auto* app = SystemUiApplicationFor(0x21001);
  REQUIRE(app);
  CHECK(std::string(app->path) == "game:\\system\\signin.xex");
  CHECK(app->request_size == 20);
  CHECK(app->user_offset == 4);
  CHECK(app->resource_id == 3);
  const auto* card = SystemUiApplicationFor(0x21008);
  REQUIRE(card);
  CHECK(card != app);
  CHECK(card->request_size == 544);
  CHECK(card->user_offset == 0);
  CHECK(card->resource_id == 13);
  CHECK_FALSE(SystemUiApplicationFor(0x21002));
  const auto* create = SystemUiApplicationFor(0x2100E);
  REQUIRE(create);
  CHECK(std::string(create->path) == "game:\\system\\createprofile.xex");
  CHECK(create->request_size == 136);
  CHECK(create->resource_id == 14);
  const auto* guide = SystemUiApplicationFor(0x21015);
  REQUIRE(guide);
  CHECK(std::string(guide->path) == "game:\\system\\hud.xex");
  CHECK(guide->request_size == 12);
  CHECK(guide->user_offset == 4);
  CHECK(guide->resource_id == 0);
  const auto* selector = SystemUiApplicationFor(0x2100B);
  REQUIRE(selector);
  CHECK(std::string(selector->path) == "game:\\system\\deviceselector.xex");
  CHECK(selector->request_size == 40);
  CHECK(selector->user_offset == 4);
  CHECK(selector->resource_id == 2);
}

TEST_CASE("Sign-in ANY_USER session preserves exclusivity and validates HUD Ex ABI", "[sdk][signin]") {
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime(); auto* keyboard=&fixture.GetInput();
  REQUIRE(StateOf(rt).AttachSystems(nullptr,nullptr,nullptr,keyboard,nullptr,{}));
  REQUIRE(keyboard->GetSettings().mnk_mode.Set(true));
  auto binding=REX_SETTINGS().Find("hid.keybind_b");REQUIRE(binding);
  const auto prior=(*binding)->GetText();const auto layer=(*binding)->GetLayer();
  rex::ScopeGuard restore([&] {CHECK((*binding)->Reset());
    if(layer!=rex::SettingLayer::kDefault)CHECK((*binding)->SetText(layer,prior));});
  REQUIRE((*binding)->SetText(rex::SettingLayer::kRuntime,rex::VirtualKeyToString(rex::VirtualKey::kBack)));
  auto xam = (&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI());
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x90105000, 3}) == 0);
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 4));
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0xFF));
  CHECK(xam->GetSystemUiSession().user == 0xFF);
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  REQUIRE(xam->EnableSystemUiInput(0xFF, true));
  CHECK_FALSE(xam->EnableSystemUiInput(4, true));
  PPCContext ctx{};
  auto* memory = (&GetAddressSpace(rt));
  const auto stroke = memory->SystemHeapAlloc(8);
  REQUIRE(stroke);
  std::memset(memory->TranslateVirtual(stroke), 0xA5, 8);
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystrokeHudEx);
  CHECK(ctx.r3.u32 == 4306); // ERROR_EMPTY: not visible; no output write.
  CHECK(memory->TranslateVirtual(stroke)[0] == 0xA5);
  REQUIRE(xam->SetSystemUiVisible(0x1000));
  rex::InputEvent key{.key=rex::VirtualKey::kBack};
  key.type=rex::InputEvent::Type::kKeyDown;keyboard->OnInputEvent(key);
  const auto pad = memory->SystemHeapAlloc(sizeof(rex::xam::X_INPUT_STATE));
  REQUIRE(pad);
  ctx.r3.u64 = 0; ctx.r4.u64 = 1; ctx.r5.u64 = pad;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetState);
  CHECK(ctx.r3.u32 == 0);
  auto* pad_state = memory->TranslateVirtual<rex::xam::X_INPUT_STATE*>(pad);
  CHECK(uint16_t(pad_state->gamepad.buttons) == 0);
  ctx.r3.u64 = 0; ctx.r4.u64 = 0x80000000; ctx.r5.u64 = pad;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetState);
  CHECK(ctx.r3.u32 == 0);
  CHECK((uint16_t(pad_state->gamepad.buttons) & rex::xam::kXInputGamepadB) != 0);
  memory->SystemHeapFree(pad);
  // The foreground title cannot consume any controller while Sign-in owns FF.
  for (uint32_t user = 0; user < 4; ++user) {
    ctx.r3.u64 = user; ctx.r4.u64 = 1; ctx.r5.u64 = stroke;
    Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystroke);
    CHECK(ctx.r3.u32 == 4306);
  }
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystrokeHudEx);
  REQUIRE(ctx.r3.u32 == 0);
  const auto* actual = memory->TranslateVirtual<rex::xam::X_INPUT_KEYSTROKE*>(stroke);
  CHECK(actual->virtual_key == uint16_t(rex::VirtualKey::kXInputPadB));
  CHECK(actual->user_index == 0);
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystrokeHudEx);
  CHECK(ctx.r3.u32 == 4306); // bounded scan, no duplicate delivery
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = stroke; ctx.r5.u64 = 0;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystrokeHudEx);
  CHECK(ctx.r3.u32 == 160); // BAD_ARGUMENTS: output is r5, not r4.
  ctx.r3.u64 = 4; ctx.r5.u64 = stroke;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystrokeHudEx);
  CHECK(ctx.r3.u32 == 160);
  REQUIRE(xam->RequestSystemUiClose());
  CHECK(xam->GetSystemUiSession().visible);
  REQUIRE(xam->EndSystemUi(0x1000));
  CHECK_FALSE(xam->GetSystemUiSession().visible);
  CHECK(xam->BeginSystemUi(0x1000, 0xFF, 0xFF));
  CHECK_FALSE(xam->GetSystemUiSession().close_requested);
  CHECK(xam->EndSystemUi(0x1000));
  // Guide requests Sign-in for controller 0. XUI still polls ANY_USER;
  // that query must be restricted to the session owner, not rejected.
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 1));
  REQUIRE(xam->EnableSystemUiInput(1, true));
  REQUIRE(xam->SetSystemUiVisible(0x1000));
  key.type=rex::InputEvent::Type::kKeyUp;keyboard->OnInputEvent(key);
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystrokeHudEx);
  CHECK(ctx.r3.u32 == 4306); // controller 1 cannot consume controller 0's event
  REQUIRE(xam->EndSystemUi(0x1000));
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  REQUIRE(xam->EnableSystemUiInput(0xFF, true)); // original Sign-in enables all controllers
  CHECK(xam->GetSystemUiSession().user == 0); // enable does not transfer ownership
  REQUIRE(xam->SetSystemUiVisible(0x1000));
  ctx.r3.u64 = 1; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystrokeHudEx);
  CHECK(ctx.r3.u32 == 4306);
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystrokeHudEx);
  CHECK(ctx.r3.u32 == 4306); // No stale release from the already closed session.
  key.type=rex::InputEvent::Type::kKeyDown;keyboard->OnInputEvent(key); // A fresh press belongs to the new controller-0 session.
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamInputGetKeystrokeHudEx);
  REQUIRE(ctx.r3.u32 == 0);
  CHECK(actual->user_index == 0);
  CHECK(actual->virtual_key == uint16_t(rex::VirtualKey::kXInputPadB));
  REQUIRE(xam->EndSystemUi(0x1000));
  memory->SystemHeapFree(stroke);
}

TEST_CASE("System apps can queue a successor before their first scene is visible", "[sdk][profile-ui][profile-ui-initial-request]") {
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  struct RestoreFlags {
    struct Saved {rex::SettingBase* setting;std::string text;rex::SettingLayer layer;};
    std::vector<Saved> values;
    ~RestoreFlags(){for(const auto& entry:values) {CHECK(entry.setting->Reset());
      if(entry.layer!=rex::SettingLayer::kDefault)CHECK(entry.setting->SetText(entry.layer,entry.text));}}
  } flags;
  for(const auto* name:{"hid.guide_button","hid.guide_chord_ms","hid.guide_chord_users","hid.keybind_guide"}) {
    auto setting=REX_SETTINGS().Find(name);REQUIRE(setting);
    flags.values.push_back({*setting,(*setting)->GetText(),(*setting)->GetLayer()});
  }

  auto xam = (&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI());
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x90105000, 3}) == 0);
  bool accepted = false, deferred_close = false, still_owned = false;
  auto caller = rt.GetThreadRegistry().LaunchHostThread("original UI contract",512*1024,[&] {
    // Install the real host, but represent only its pre-scene lifecycle phase.
    // No drawing/frame callback is run, and no original app is initialized.
    dash17559::InstallSystemUi(rt);
    if (!xam->BeginSystemUi(0x1000, 0xFF, 0)) return 1;
    PPCContext ctx{}; ctx.r3.u64 = 0; ctx.r4.u64 = 1;
    Call(fixture,ctx,(&GetAddressSpace(rt))->GetVirtualMembase(),__imp__XamShowCreateProfileUI);
    const auto session = xam->GetSystemUiSession();
    accepted = ctx.r3.u32 == 0;
    deferred_close = session.close_requested && !session.visible;
    still_owned = session.module == 0x1000 && xam->FindSystemApp(0xFF).has_value();
    xam->EndSystemUi(0x1000);
    dash17559::ReleaseSystemUiHost(rt); // Discard queued work; never construct a scene.
    return 0;
  });
  REQUIRE(caller);
  uint64_t timeout = uint64_t(-100'000'000ll);
  REQUIRE(rt.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller,0,0,0,&timeout) == 0);
  CloseOwned(rt.GetObjectTable(),*caller);
  CHECK(accepted);
  CHECK(deferred_close);
  CHECK(still_owned);
}

TEST_CASE("XamAppLoad reaches only a title-selected original app loader", "[sdk][profile-ui][app-load]") {
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  auto* memory = (&GetAddressSpace(rt));
  auto* base = memory->GetVirtualMembase();
  auto xam = (&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI());
  // Exports by ordinal, as the original Guide's import thunks reach them.
  const auto xam_handle=rt.GetModuleRegistry().GetModule("xam.xex");REQUIRE(xam_handle);
  Pinned<ModuleObject> kernel_xam(rt.GetObjectTable().Get(*xam_handle));REQUIRE(kernel_xam);
  const auto request_load=GetFunctionDispatcher(rt).GetFunction(kernel_xam->GetExportAddress(rt,0x248));
  const auto load=GetFunctionDispatcher(rt).GetFunction(kernel_xam->GetExportAddress(rt,0x244));
  REQUIRE(request_load); REQUIRE(load);
  const auto name = memory->SystemHeapAlloc(32);
  std::strcpy(memory->TranslateVirtual<char*>(name), "createprofile.xex");
  const auto payload = memory->SystemHeapAlloc(512);
  PPCContext ctx{};
  auto call_load = [&](uint32_t buffer, uint32_t size, uint32_t overlapped = 0, uint32_t handle = 0) {
    ctx.r3.u64 = name; ctx.r4.u64 = 2; ctx.r5.u64 = buffer; ctx.r6.u64 = size;
    ctx.r7.u64 = overlapped; ctx.r8.u64 = handle;
    load(ctx, base);
    return ctx.r3.u32;
  };
  // No loader: the gate is refused and a load fails as 816CDF7C does.
  ctx.r3.u64 = 0xDEADBEEF;
  request_load(ctx, base);
  CHECK(ctx.r3.u32 == 0);
  CHECK(call_load(0, 0) == 0x80004005);
  std::string module;
  uint32_t calls = 0, user = 0, buffer = 0, size = 0, status = 0;
  xam->system_app_load = [&](const std::string& m, uint32_t u, uint32_t b, uint32_t s) {
    module = m; user = u; buffer = b; size = s; ++calls; return status;
  };
  ctx.r3.u64 = 0xDEADBEEF;
  Call(fixture,ctx,base,__imp__XamAppRequestLoad);
  CHECK(ctx.r3.u32 == 1);
  CHECK(call_load(0, 0) == 0);
  REQUIRE(calls == 1);
  CHECK(module == "createprofile.xex"); CHECK(user == 2); CHECK(buffer == 0); CHECK(size == 0);
  CHECK(call_load(payload, 400) == 0);
  CHECK(buffer == payload); CHECK(size == 256);   // 816CDEB0 keeps at most 256 bytes
  CHECK(call_load(0, 64) == 0); CHECK(size == 0); // no buffer, nothing to copy
  status = 0x80070490;
  CHECK(call_load(0, 0) == 0x80070490);
  // OVERLAPPED completion and the app handle are not implemented: refused and
  // completed with the original failure code, never reported as loaded.
  const auto io = memory->SystemHeapAlloc(sizeof(rex::kernel::XOVERLAPPED));
  std::memset(memory->TranslateVirtual(io), 0, sizeof(rex::kernel::XOVERLAPPED));
  const auto before = calls;
  uint32_t overlapped_result = 0;
  auto caller = rt.GetThreadRegistry().LaunchHostThread("original UI contract",512*1024,[&] {
    overlapped_result = call_load(0, 0, io); // completion runs on a guest thread
    return 0;
  });
  REQUIRE(caller);
  uint64_t timeout = uint64_t(-100'000'000ll);
  REQUIRE(rt.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller,0,0,0,&timeout) == 0);
  CloseOwned(rt.GetObjectTable(),*caller);
  CHECK(overlapped_result == 0x80004001);
  CHECK(memory->TranslateVirtual<rex::kernel::XOVERLAPPED*>(io)->result == 1627);
  CHECK(call_load(0, 0, 0, payload) == 0x80004001);
  CHECK(calls == before);
  memory->SystemHeapFree(io);
  memory->SystemHeapFree(payload);
  memory->SystemHeapFree(name);
  xam->system_app_load = {};
}

TEST_CASE("Guide's direct Create Profile loads the original app without a request", "[sdk][profile-ui][app-load]") {
  using namespace dash17559;
  CHECK(SystemUiMessageForLoad("createprofile.xex") == 0x2100E);
  CHECK(SystemUiMessageForLoad("CreateProfile.XEX") == 0x2100E);
  CHECK(SystemUiMessageForLoad("signin.xex") == 0);
  CHECK(SystemUiMessageForLoad("createprofile.xex2") == 0);
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  struct RestoreFlags {
    struct Saved {rex::SettingBase* setting;std::string text;rex::SettingLayer layer;};
    std::vector<Saved> values;
    ~RestoreFlags(){for(const auto& entry:values) {CHECK(entry.setting->Reset());
      if(entry.layer!=rex::SettingLayer::kDefault)CHECK(entry.setting->SetText(entry.layer,entry.text));}}
  } flags;
  for(const auto* name:{"hid.guide_button","hid.guide_chord_ms","hid.guide_chord_users","hid.keybind_guide"}) {
    auto setting=REX_SETTINGS().Find(name);REQUIRE(setting);
    flags.values.push_back({*setting,(*setting)->GetText(),(*setting)->GetLayer()});
  }

  auto xam = (&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI());
  // The original Guide (hud 913E5930, resource 0) owns the session for controller 1.
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x913E5930, 0}) == 0);
  uint32_t granted = 0, unknown = 0, with_buffer = 0, loaded = 1;
  bool kept_open = false, deferred_close = false, still_owned = false;
  auto caller = rt.GetThreadRegistry().LaunchHostThread("original UI contract",512*1024,[&] {
    // Real host; only the pre-scene phase is represented, no app is initialized.
    dash17559::InstallSystemUi(rt);
    if (!xam->BeginSystemUi(0x1000, 0xFF, 1)) return 1;
    auto* memory = (&GetAddressSpace(rt));
    const auto name = memory->SystemHeapAlloc(32);
    auto* base = memory->GetVirtualMembase();
    PPCContext ctx{};
    Call(fixture,ctx,base,__imp__XamAppRequestLoad);
    granted = ctx.r3.u32;
    auto load = [&](const char* module, uint32_t buffer, uint32_t size) {
      std::strcpy(memory->TranslateVirtual<char*>(name), module);
      ctx.r3.u64 = name; ctx.r4.u64 = 1; ctx.r5.u64 = buffer; ctx.r6.u64 = size;
      ctx.r7.u64 = 0; ctx.r8.u64 = 0;
      Call(fixture,ctx,base,__imp__XamAppLoad);
      return ctx.r3.u32;
    };
    unknown = load("signin.xex", 0, 0);
    with_buffer = load("createprofile.xex", name, 16);
    kept_open = !xam->GetSystemUiSession().close_requested;
    // hud 913EA808: XamAppLoad("createprofile.xex", controller, 0, 0, 0, 0).
    loaded = load("createprofile.xex", 0, 0);
    const auto session = xam->GetSystemUiSession();
    deferred_close = session.close_requested;
    still_owned = session.module == 0x1000 && session.user == 1;
    memory->SystemHeapFree(name);
    xam->EndSystemUi(0x1000);
    dash17559::ReleaseSystemUiHost(rt); // Discard queued work; never construct a scene.
    return 0;
  });
  REQUIRE(caller);
  uint64_t timeout = uint64_t(-100'000'000ll);
  REQUIRE(rt.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller,0,0,0,&timeout) == 0);
  CloseOwned(rt.GetObjectTable(),*caller);
  CHECK(granted == 1);
  CHECK(unknown == 0x80070490);       // no silent fallback to another app
  CHECK(with_buffer == 0x80004001);   // buffer layout not verified: refused
  CHECK(kept_open);
  CHECK(loaded == 0);                 // queued behind the Guide at the frame boundary
  CHECK(deferred_close);
  CHECK(still_owned);
}

TEST_CASE("Closing the original application stack differs from closing its top app", "[sdk][profile-ui][profile-ui-stack]") {
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  auto xam = (&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI());
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x90105C98, 14}) == 0);
  // Keep the pre-patch negative control runnable, rather than failing to compile
  // against its older session structure.
  auto stack_requested = []<typename Session>(const Session& session) {
    if constexpr (requires { session.close_stack_requested; }) return session.close_stack_requested;
    else return false;
  };
  PPCContext ctx{};
  auto* base = (&GetAddressSpace(rt))->GetVirtualMembase();
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  Call(fixture,ctx,base,__imp__XamAppUnloadSelf);
  CHECK(ctx.r3.u32 == 0);
  CHECK(xam->GetSystemUiSession().close_requested);
  CHECK_FALSE(stack_requested(xam->GetSystemUiSession()));
  REQUIRE(xam->EndSystemUi(0x1000));
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  REQUIRE(xam->SetSystemUiVisible(0x1000));
  ctx.r3.u64 = 1;
  Call(fixture,ctx,base,__imp__XamAppUnloadStack);
  CHECK(ctx.r3.u32 == 87);
  CHECK_FALSE(xam->GetSystemUiSession().close_requested);
  ctx.r3.u64 = 0;
  Call(fixture,ctx,base,__imp__XamAppUnloadStack);
  CHECK(ctx.r3.u32 == 0);
  CHECK(xam->GetSystemUiSession().close_requested);
  CHECK(stack_requested(xam->GetSystemUiSession()));
  CHECK(xam->GetSystemUiSession().visible); // Destruction waits for the frame boundary.
  CHECK(xam->FindSystemApp(0xFF).has_value());
  // A queued successor also requests close-self. It must not downgrade the
  // earlier close-stack, or CreateProfile's completed parents will be restored.
  REQUIRE(xam->RequestSystemUiClose());
  CHECK(stack_requested(xam->GetSystemUiSession()));
  REQUIRE(xam->EndSystemUi(0x1000));
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  CHECK_FALSE(stack_requested(xam->GetSystemUiSession()));
  CHECK_FALSE(xam->GetSystemUiSession().close_requested);
  REQUIRE(xam->EndSystemUi(0x1000));
}

TEST_CASE("System UI host exports reject calls without an active session", "[sdk][profile-ui]") {
  using rex::kernel::X_STATUS;
  using rex::kernel::X_RESULT;
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  PPCContext ctx{};
  auto* base = (&GetAddressSpace(rt))->GetVirtualMembase();
  ctx.r3.u64 = 0; ctx.r4.u64 = 1;
  Call(fixture,ctx,base,__imp__XamEnableSystemAppInput);
  CHECK(ctx.r3.u32 == 1627);
  ctx.r3.u64 = 0;
  Call(fixture,ctx,base,__imp__XamAppUnloadSelf);
  CHECK(ctx.r3.u32 == 1627);
  const auto stroke = (&GetAddressSpace(rt))->SystemHeapAlloc(8);
  ctx.r3.u64 = 0; ctx.r4.u64 = stroke;
  Call(fixture,ctx,base,__imp__XamInputGetKeystrokeHud);
  CHECK(ctx.r3.u32 == X_ERROR_EMPTY);
  (&GetAddressSpace(rt))->SystemHeapFree(stroke);
}

TEST_CASE("System UI sessions isolate input and defer closure until the host releases ownership", "[sdk][profile-ui]") {
  using rex::kernel::X_STATUS;
  using rex::kernel::X_RESULT;
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  auto& input=fixture.GetInput();
  REQUIRE(StateOf(rt).AttachSystems(nullptr,nullptr,nullptr,&input,nullptr,{}));
  REQUIRE(input.GetSettings().mnk_mode.Set(true));
  auto xam = (&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI());
  REQUIRE(xam);
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x9010D8A0, 13}) == 0);
  CHECK_FALSE(xam->BeginSystemUi(0x2000, 0xFF, 0));
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 4));
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  CHECK_FALSE(xam->SetSystemUiVisible(0x2000));
  REQUIRE(xam->SetSystemUiVisible(0x1000));
  CHECK_FALSE(xam->RequestSystemUiResume(0x2000));
  REQUIRE(xam->RequestSystemUiResume(0x1000));
  CHECK_FALSE(xam->ConsumeSystemUiResume(0x2000));
  CHECK(xam->ConsumeSystemUiResume(0x1000));
  CHECK_FALSE(xam->ConsumeSystemUiResume(0x1000));
  PPCContext ctx{};
  auto* base = (&GetAddressSpace(rt))->GetVirtualMembase();
  Call(fixture,ctx,base,__imp__XamHudGetUserIndex);
  CHECK(ctx.r3.u32 == 0);
  Call(fixture,ctx,base,__imp__XamIsUIActive);
  CHECK(ctx.r3.u32 == 1);
  ctx.r3.u64 = 1; ctx.r4.u64 = 1;
  Call(fixture,ctx,base,__imp__XamEnableSystemAppInput);
  CHECK(ctx.r3.u32 == 1627);
  ctx.r3.u64 = 0; ctx.r4.u64 = 1;
  Call(fixture,ctx,base,__imp__XamEnableSystemAppInput);
  CHECK(ctx.r3.u32 == 0);
  CHECK(xam->GetSystemUiSession().input_enabled);
  const auto state = (&GetAddressSpace(rt))->SystemHeapAlloc(sizeof(rex::xam::X_INPUT_STATE));
  std::memset(base + state, 0xA5, sizeof(rex::xam::X_INPUT_STATE));
  ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = state;
  Call(fixture,ctx,base,__imp__XamInputGetState);
  CHECK(ctx.r3.u32 == 0);
  CHECK(std::all_of(base + state, base + state + sizeof(rex::xam::X_INPUT_STATE),
                    [](uint8_t byte) { return byte == 0; }));
  (&GetAddressSpace(rt))->SystemHeapFree(state);
  const auto before = uint32_t(rex::Clock::GetGuestUptimeMillis());
  ctx.r3.u64 = 0xDEADC0DE;
  Call(fixture,ctx,base,__imp__GetTickCount);
  const auto after = uint32_t(rex::Clock::GetGuestUptimeMillis());
  CHECK(uint32_t(ctx.r3.u32 - before) <= uint32_t(after - before));
  const auto stroke = (&GetAddressSpace(rt))->SystemHeapAlloc(8);
  std::memset(base + stroke, 0xA5, 8);
  ctx.r3.u64 = 0; ctx.r4.u64 = 1; ctx.r5.u64 = stroke;
  Call(fixture,ctx,base,__imp__XamInputGetKeystroke);
  CHECK(ctx.r3.u32 == X_ERROR_EMPTY); // foreground app cannot drain the HUD queue
  CHECK(base[stroke] == 0xA5);
  ctx.r3.u64 = 1; ctx.r4.u64 = stroke;
  Call(fixture,ctx,base,__imp__XamInputGetKeystrokeHud);
  CHECK(ctx.r3.u32 == X_ERROR_EMPTY); // another user cannot consume this session
  ctx.r3.u64 = 1;
  Call(fixture,ctx,base,__imp__XamAppUnloadStack);
  CHECK(ctx.r3.u32 == 87);
  CHECK_FALSE(xam->GetSystemUiSession().close_requested);
  Call(fixture,ctx,base,__imp__XamAppUnloadSelf);
  CHECK(ctx.r3.u32 == 0);
  CHECK(xam->GetSystemUiSession().close_requested);
  CHECK(xam->GetSystemUiSession().visible);
  CHECK_FALSE(xam->EndSystemUi(0x2000));
  REQUIRE(xam->EndSystemUi(0x1000));
  CHECK_FALSE(xam->RequestSystemUiResume(0x1000));
  Call(fixture,ctx,base,__imp__XamHudGetUserIndex);
  CHECK(ctx.r3.u32 == 0xFF);
  Call(fixture,ctx,base,__imp__XamIsUIActive);
  CHECK(ctx.r3.u32 == 0);
  CHECK_FALSE(xam->GetSystemUiSession().input_enabled);
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  CHECK_FALSE(xam->GetSystemUiSession().close_requested);
  CHECK_FALSE(xam->GetSystemUiSession().visible);
  CHECK(xam->EndSystemUi(0x1000));
  (&GetAddressSpace(rt))->SystemHeapFree(stroke);
}

namespace {
class SystemUiProbe final {
 public:
  explicit SystemUiProbe(uint8_t* base) : base_(base) {}
  rex::kernel::X_HRESULT DispatchMessageSync(uint32_t message, uint32_t buffer,
                                   uint32_t length) {
    ++calls;
    last_message = message;
    words.clear();
    for (uint32_t i = 0; i < length / 4; ++i)
      words.push_back(rex::memory::load_and_swap<uint32_t>(base_ + buffer + i * 4));
    return result;
  }
  uint8_t* base_;
  unsigned calls = 0;
  uint32_t last_message = 0;
  std::vector<uint32_t> words;
  rex::kernel::X_HRESULT result = static_cast<rex::kernel::X_HRESULT>(0x80004005u);
};
}

TEST_CASE("Profile UI requests reach the system application without invented signin notifications",
          "[sdk][profile-ui]") {
  using rex::kernel::X_STATUS;
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  SystemUiProbe probe_owner(rt.GetMembase().GetBase());auto* probe=&probe_owner;
  auto& system_ui=REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI();
  system_ui.system_ui_request=[&](uint32_t message,uint32_t buffer,uint32_t size) {
    return probe->DispatchMessageSync(message,buffer,size);
  };
  rex::ScopeGuard detach_probe([&] {system_ui.system_ui_request={};});
  auto listener_handle=rt.GetNotificationBroadcaster().AddNotifyListener(1,10);REQUIRE(listener_handle);
  rex::ScopeGuard close_listener([&] {CloseOwned(rt.GetObjectTable(),*listener_handle);});
  Pinned<INotifyListenerQueue> listener(rt.GetObjectTable().Get(*listener_handle));REQUIRE(listener);
  uint32_t id = 0, param = 0;
  while (listener->Dequeue(&id, &param)) {}
  PPCContext ctx{};
  ctx.r3.u64 = 1; ctx.r4.u64 = 2;
  Call(fixture,ctx,(&GetAddressSpace(rt))->GetVirtualMembase(),__imp__XamShowSigninUI);
  CHECK(ctx.r3.u32 == 1627); // Original wrapper maps failed UI dispatch to ERROR_FUNCTION_FAILED.
  CHECK(probe->calls == 1);
  CHECK(probe->last_message == 0x21001);
  CHECK(probe->words == std::vector<uint32_t>{0, 0xFF, 1, 2, 0});
  CHECK_FALSE(listener->Dequeue(&id, &param));
  ctx.r3.u64 = 0;
  Call(fixture,ctx,(&GetAddressSpace(rt))->GetVirtualMembase(),__imp__XamShowGamerCardUI);
  CHECK(ctx.r3.u32 == 1627);
  CHECK(probe->calls == 2);
  CHECK(probe->last_message == 0x21008);
  CHECK(probe->words == std::vector<uint32_t>(544 / 4, 0));
  CHECK_FALSE(listener->Dequeue(&id, &param));
  ctx.r3.u64 = 0; ctx.r4.u64 = 1;
  Call(fixture,ctx,(&GetAddressSpace(rt))->GetVirtualMembase(),__imp__XamShowCreateProfileUI);
  CHECK(ctx.r3.u32 == 1627);
  CHECK(probe->calls == 3);
  CHECK(probe->last_message == 0x2100E);
  auto expected = std::vector<uint32_t>(34, 0);
  expected[1] = 1;
  CHECK(probe->words == expected);
  const auto extra = (&GetAddressSpace(rt))->SystemHeapAlloc(114);
  std::memset((&GetAddressSpace(rt))->TranslateVirtual(extra), 0xAC, 114);
  ctx.r3.u64 = 2; ctx.r4.u64 = 11; ctx.r5.u64 = extra;
  Call(fixture,ctx,(&GetAddressSpace(rt))->GetVirtualMembase(),__imp__XamShowCreateProfileUIEx);
  CHECK(ctx.r3.u32 == 1627);
  CHECK(probe->words[0] == 2);
  CHECK(probe->words[1] == 11);
  CHECK(probe->words[4] == 0xACACACAC);
  CHECK(probe->words[32] == 0xACAC0000);
  CHECK(probe->words[33] == 0);
  (&GetAddressSpace(rt))->SystemHeapFree(extra);
  CHECK_FALSE(listener->Dequeue(&id, &param));
  ctx.r3.u64 = 0;
  Call(fixture,ctx,(&GetAddressSpace(rt))->GetVirtualMembase(),__imp__XamShowGuideUI);
  CHECK(ctx.r3.u32 == 1627);
  CHECK(probe->last_message == 0x21015);
  CHECK(probe->words == std::vector<uint32_t>{0, 0, 0});
  const auto count = probe->calls;
  ctx.r3.u64 = 4;
  Call(fixture,ctx,(&GetAddressSpace(rt))->GetVirtualMembase(),__imp__XamShowGuideUI);
  CHECK(ctx.r3.u32 == 87);
  CHECK(probe->calls == count);
  CHECK_FALSE(listener->Dequeue(&id, &param));
  ctx.r3.u64 = 0; ctx.r4.u64 = 1; ctx.r5.u64 = 0x1010002;
  Call(fixture,ctx,(&GetAddressSpace(rt))->GetVirtualMembase(),__imp__XamShowSigninUIp);
  CHECK(ctx.r3.u32 == 1627);
  CHECK(probe->last_message == 0x21001);
  CHECK(probe->words == std::vector<uint32_t>{0, 0, 1, 0x1010002, 0});
}

TEST_CASE("Sign-in request propagates host outcomes without changing the user", "[sdk][signin]") {
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  auto xam = (&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI());
  auto listener_handle=rt.GetNotificationBroadcaster().AddNotifyListener(1,10);REQUIRE(listener_handle);
  rex::ScopeGuard close_listener([&] {CloseOwned(rt.GetObjectTable(),*listener_handle);});
  Pinned<INotifyListenerQueue> listener(rt.GetObjectTable().Get(*listener_handle));REQUIRE(listener);
  uint32_t id = 0, param = 0;
  while (listener->Dequeue(&id, &param)) {}
  for (const auto [host_result, expected] : {
       std::pair<uint32_t, uint32_t>{0x80004005, 1627}, {0x8007000E, 14}, {0, 0}}) {
    bool delivered = false;
    xam->system_ui_request = [&](uint32_t message, uint32_t request, uint32_t size) {
      delivered = true;
      CHECK(message == 0x21001);
      REQUIRE(size == 20);
      auto* words = (&GetAddressSpace(rt))->TranslateVirtual<rex::be_u32*>(request);
      CHECK(uint32_t(words[0]) == 0);
      CHECK(uint32_t(words[1]) == 0xFF);
      CHECK(uint32_t(words[2]) == 1);
      CHECK(uint32_t(words[3]) == 2);
      CHECK(uint32_t(words[4]) == 0);
      return static_cast<rex::kernel::X_HRESULT>(host_result);
    };
    PPCContext ctx{};
    ctx.r3.u64 = 1; ctx.r4.u64 = 2;
    Call(fixture,ctx,(&GetAddressSpace(rt))->GetVirtualMembase(),__imp__XamShowSigninUI);
    CHECK(delivered);
    CHECK(ctx.r3.u32 == expected);
    CHECK_FALSE(listener->Dequeue(&id, &param));
  }
  xam->system_ui_request = {};
}

TEST_CASE("System UI registration preserves module ownership and rejects occupied slots", "[sdk][profile-ui]") {
  using rex::kernel::X_STATUS;
  rex::test::KernelFixture fixture;
  auto& xam=REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI();
  CHECK_FALSE(xam.FindSystemApp(0xFF));
  CHECK(xam.RegisterSystemApp({0, 0xFF, 0x9010D8A0, 13}) == 1627);
  CHECK(xam.RegisterSystemApp({0x1000, 0xFF, 0, 13}) == 1627);
  CHECK(xam.RegisterSystemApp({0x1000, 0xFF, 0x9010D8A0, 13}) == 0);
  CHECK(xam.RegisterSystemApp({0x2000, 0xFF, 0x90105000, 3}) == 1627);
  REQUIRE(xam.FindSystemApp(0xFF));
  CHECK(xam.FindSystemApp(0xFF)->resource_id == 13);
  CHECK(xam.UnregisterSystemApp(0x2000, 0xFF) == 0);
  CHECK(xam.FindSystemApp(0xFF)->callback == 0x9010D8A0);
  CHECK(xam.UnregisterSystemApp(0x1000, 0xFF) == 0);
  CHECK_FALSE(xam.FindSystemApp(0xFF));
  CHECK(xam.RegisterSystemApp({0x2000, 0xFF, 0x90105000, 3}) == 0);
}

extern "C" REX_FUNC(__imp__XamAllocEx);
extern "C" REX_FUNC(__imp__XamFree);
TEST_CASE("System UI allocation writes a real guest pointer using the extended ABI", "[sdk][profile-ui]") {
  using rex::kernel::X_STATUS;
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  auto* memory = (&GetAddressSpace(rt));
  const auto output = memory->SystemHeapAlloc(4);
  REQUIRE(output != 0);
  auto* result = memory->TranslateVirtual<rex::be_u32*>(output);
  *result = 0;
  PPCContext ctx{};
  ctx.r3.u64 = 0x20000000; ctx.r4.u64 = 0; ctx.r5.u64 = 808; ctx.r6.u64 = output;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamAllocEx);
  CHECK(ctx.r3.u32 == 0);
  REQUIRE(uint32_t(*result) != 0);
  auto allocated = uint32_t(*result);
  // Exercise the entire constructor allocation, including its last member.
  std::memset(memory->TranslateVirtual(allocated), 0xA5, 808);
  CHECK(memory->TranslateVirtual(allocated)[807] == 0xA5);
  ctx.r3.u64 = allocated;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamFree);
  CHECK(ctx.r3.u32 == 0);
  *result = 0xDEADBEEF;
  ctx.r3.u64 = 0x20000000; ctx.r4.u64 = 0x40000000; ctx.r5.u64 = 808; ctx.r6.u64 = output;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamAllocEx);
  // Official v0.11 implements this physical class; validate usable storage and release it.
  REQUIRE(ctx.r3.u32==0);const auto physical=uint32_t(*result);REQUIRE(physical);
  std::memset(memory->TranslateVirtual(physical),0xA5,808);
  CHECK(memory->TranslateVirtual(physical)[807]==0xA5);
  ctx.r3.u64=physical;Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamFree);
  uint32_t physical_size=~0u;REQUIRE(memory->LookupHeap(physical)->QuerySize(physical,&physical_size));
  CHECK(physical_size==0);
  *result=0xDEADBEEF;ctx.r3.u64=0;ctx.r4.u64=1;ctx.r5.u64=808;ctx.r6.u64=output;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamAllocEx);
  CHECK(ctx.r3.u32==0x80004001);CHECK(uint32_t(*result)==0);
  memory->SystemHeapFree(output);
}

#include <chrono>
#include <fstream>
#include "synthetic_xex.h"
namespace {
void ProfileModuleProbe(PPCContext&, uint8_t*) {}
void RegisterProfileProbe(rex::kernel::IFunctionRegistrar* registrar) {
  registrar->SetFunction(0x82010000, ProfileModuleProbe);
}
}
TEST_CASE("Unloading a system module clears code registered by a game path alias", "[sdk][profile-ui]") {
  using rex::kernel::X_STATUS;
  const auto root = std::filesystem::temp_directory_path() /
      ("x360n-profile-module-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "system");
  struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
  const auto bytes = x360n::test::BuildXex({}, nullptr, 0, x360n::test::Entry::kReturnZero);
  { std::ofstream file(root / "system" / "profile.xex", std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, root);
  auto& rt=fixture.GetRuntime();auto* state=&static_cast<ModuleRegistry&>(rt.GetModuleRegistry());
  state->RegisterRecompiledModule("profile.dll", "game:\\system\\profile.xex", "");
  auto module_handle = state->LoadUserModule("game:\\system\\profile.xex", false);
  REQUIRE(module_handle);Pinned<ModuleObject> module(rt.GetObjectTable().Get(*module_handle));
  REQUIRE(module);
  const std::string guest_path="game:\\system\\profile.xex";
  // Stand in for the native DLL registrar, using the same public registration
  // path. The loaded XEX path has already been resolved through the VFS.
  REQUIRE(module->GetPath() != guest_path);
  module->user.recompiled_key=guest_path;
  auto* dispatcher = (&GetFunctionDispatcher(rt));
  REQUIRE(dispatcher->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
  dispatcher->RegisterModule(guest_path, 0x82010000, RegisterProfileProbe);
  REQUIRE((dispatcher->GetFunction(0x82010000) == ProfileModuleProbe));
  state->UnloadUserModule(*module_handle, false);
  CHECK(dispatcher->GetFunction(0x82010000) == nullptr);
  // A second load must be able to install a table at the same image address.
  CHECK(dispatcher->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
}

extern "C" REX_FUNC(__imp__XMemAlloc);
extern "C" REX_FUNC(__imp__XMemFree);
TEST_CASE("XMem virtual allocation owns writable aligned memory and rejects unsupported physical classes", "[sdk][profile-ui]") {
  using rex::kernel::X_STATUS;
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  PPCContext ctx{};
  auto* memory = (&GetAddressSpace(rt));
  ctx.r3.u64 = 68; ctx.r4.u64 = 0x24800000;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XMemAlloc);
  const auto pointer = ctx.r3.u32;
  REQUIRE(pointer != 0);
  CHECK((pointer & 0x1F) == 0);
  std::memset(memory->TranslateVirtual(pointer), 0xA5, 68);
  CHECK(memory->TranslateVirtual(pointer)[67] == 0xA5);
  uint32_t size = 0;
  REQUIRE(memory->LookupHeap(pointer)->QuerySize(pointer, &size));
  CHECK(size >= 68);
  ctx.r3.u64 = pointer; ctx.r4.u64 = 0; // Free identifies the block from its pointer.
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XMemFree);
  REQUIRE(memory->LookupHeap(pointer)->QuerySize(pointer, &size));
  CHECK(size == 0);
  for (const auto attributes : {0x608A2002u, 0x61820005u}) {
  ctx.r3.u64 = 384; ctx.r4.u64 = attributes;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XMemAlloc);
  const auto text_pointer = ctx.r3.u32;
  REQUIRE(text_pointer != 0);
  CHECK(std::all_of(memory->TranslateVirtual(text_pointer), memory->TranslateVirtual(text_pointer) + 384,
                    [](uint8_t byte) { return byte == 0; }));
  ctx.r3.u64 = text_pointer; ctx.r4.u64 = 0;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XMemFree);
  REQUIRE(memory->LookupHeap(text_pointer)->QuerySize(text_pointer, &size));
  CHECK(size == 0);
  }
  ctx.r3.u64 = 68; ctx.r4.u64 = 0xA4800000; // Physical class is not supported here.
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XMemAlloc);
  CHECK(ctx.r3.u32 == 0);
}

TEST_CASE("HUD allocation uses the system pool and remains owned until XamFree", "[sdk][profile-ui][hud-allocation]") {
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  auto* memory = (&GetAddressSpace(rt));
  const auto output = memory->SystemHeapAlloc(4);
  REQUIRE(output != 0);
  auto* result = memory->TranslateVirtual<rex::be_u32*>(output);
  *result = 0xDEADBEEF;
  PPCContext ctx{};
  ctx.r3.u64 = 0x20160000; ctx.r4.u64 = 0x01000000; ctx.r5.u64 = 4096; ctx.r6.u64 = output;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamAllocEx);
  CHECK(ctx.r3.u32 == 0);
  REQUIRE(uint32_t(*result) != 0);
  const auto pointer = uint32_t(*result);
  std::memset(memory->TranslateVirtual(pointer), 0xA5, 4096);
  CHECK(memory->TranslateVirtual(pointer)[4095] == 0xA5);
  uint32_t size = 0;
  REQUIRE(memory->LookupHeap(pointer)->QuerySize(pointer, &size));
  CHECK(size >= 4096);
  ctx.r3.u64 = pointer;
  Call(fixture,ctx,memory->GetVirtualMembase(),__imp__XamFree);
  REQUIRE(memory->LookupHeap(pointer)->QuerySize(pointer, &size));
  CHECK(size == 0);
  memory->SystemHeapFree(output);
}
