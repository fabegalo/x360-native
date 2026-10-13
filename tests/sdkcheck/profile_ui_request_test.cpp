#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <rex/runtime.h>
#include <rex/chrono/clock.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/app_manager.h>
#include <rex/system/xnotifylistener.h>
#include "../../titles/dash_17559/src/system_ui_request.h"

extern "C" REX_FUNC(__imp__XamShowSigninUI);
extern "C" REX_FUNC(__imp__XamShowGamerCardUI);
extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHud);
extern "C" REX_FUNC(__imp__XamAppUnloadSelf);
extern "C" REX_FUNC(__imp__XamEnableSystemAppInput);
extern "C" REX_FUNC(__imp__XamInputGetKeystroke);
extern "C" REX_FUNC(__imp__XamIsUIActive);
extern "C" REX_FUNC(__imp__XamAppUnloadStack);
extern "C" REX_FUNC(__imp__XamInputGetState);
extern "C" REX_FUNC(__imp__GetTickCount);
extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHudEx);
REXCVAR_DECLARE(bool, mnk_mode);

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
}

TEST_CASE("Sign-in ANY_USER session preserves exclusivity and validates HUD Ex ABI", "[sdk][signin]") {
  const bool saved_mnk = REXCVAR_GET(mnk_mode);
  struct RestoreMnk { bool value; ~RestoreMnk() { REXCVAR_SET(mnk_mode, value); } } restore{saved_mnk};
  REXCVAR_SET(mnk_mode, true);
  rex::input::mnk::MnkInputDriver* keyboard = nullptr;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  config.input_factory = [&keyboard](bool) {
    auto input = std::make_unique<rex::input::InputSystem>(nullptr);
    auto driver = std::make_unique<rex::input::mnk::MnkInputDriver>(nullptr, 0);
    keyboard = driver.get();
    input->AddDriver(std::move(driver));
    input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
    return input;
  };
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x90105000, 3}) == 0);
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 4));
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0xFF));
  CHECK(xam->GetSystemUiSession().user == 0xFF);
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  REQUIRE(xam->EnableSystemUiInput(0xFF, true));
  CHECK_FALSE(xam->EnableSystemUiInput(4, true));
  PPCContext ctx{};
  auto* memory = rt.memory();
  const auto stroke = memory->SystemHeapAlloc(8);
  REQUIRE(stroke);
  std::memset(memory->TranslateVirtual(stroke), 0xA5, 8);
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  __imp__XamInputGetKeystrokeHudEx(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 4306); // ERROR_EMPTY: not visible; no output write.
  CHECK(memory->TranslateVirtual(stroke)[0] == 0xA5);
  REQUIRE(xam->SetSystemUiVisible(0x1000));
  rex::ui::KeyEvent key(nullptr, rex::ui::VirtualKey::kBack, 1, false, false, false, false, false);
  keyboard->OnKeyDown(key);
  const auto pad = memory->SystemHeapAlloc(sizeof(rex::input::X_INPUT_STATE));
  REQUIRE(pad);
  ctx.r3.u64 = 0; ctx.r4.u64 = 1; ctx.r5.u64 = pad;
  __imp__XamInputGetState(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 0);
  auto* pad_state = memory->TranslateVirtual<rex::input::X_INPUT_STATE*>(pad);
  CHECK(uint16_t(pad_state->gamepad.buttons) == 0);
  ctx.r3.u64 = 0; ctx.r4.u64 = 0x80000000; ctx.r5.u64 = pad;
  __imp__XamInputGetState(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 0);
  CHECK((uint16_t(pad_state->gamepad.buttons) & rex::input::X_INPUT_GAMEPAD_B) != 0);
  memory->SystemHeapFree(pad);
  // The foreground title cannot consume any controller while Sign-in owns FF.
  for (uint32_t user = 0; user < 4; ++user) {
    ctx.r3.u64 = user; ctx.r4.u64 = 1; ctx.r5.u64 = stroke;
    __imp__XamInputGetKeystroke(ctx, memory->virtual_membase());
    CHECK(ctx.r3.u32 == 4306);
  }
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  __imp__XamInputGetKeystrokeHudEx(ctx, memory->virtual_membase());
  REQUIRE(ctx.r3.u32 == 0);
  const auto* actual = memory->TranslateVirtual<rex::input::X_INPUT_KEYSTROKE*>(stroke);
  CHECK(actual->virtual_key == uint16_t(rex::ui::VirtualKey::kXInputPadB));
  CHECK(actual->user_index == 0);
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = 0x20000000; ctx.r5.u64 = stroke;
  __imp__XamInputGetKeystrokeHudEx(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 4306); // bounded scan, no duplicate delivery
  ctx.r3.u64 = 0xFF; ctx.r4.u64 = stroke; ctx.r5.u64 = 0;
  __imp__XamInputGetKeystrokeHudEx(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 160); // BAD_ARGUMENTS: output is r5, not r4.
  ctx.r3.u64 = 4; ctx.r5.u64 = stroke;
  __imp__XamInputGetKeystrokeHudEx(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 160);
  REQUIRE(xam->RequestSystemUiClose());
  CHECK(xam->GetSystemUiSession().visible);
  REQUIRE(xam->EndSystemUi(0x1000));
  CHECK_FALSE(xam->GetSystemUiSession().visible);
  CHECK(xam->BeginSystemUi(0x1000, 0xFF, 0xFF));
  CHECK_FALSE(xam->GetSystemUiSession().close_requested);
  CHECK(xam->EndSystemUi(0x1000));
  memory->SystemHeapFree(stroke);
}

TEST_CASE("System UI host exports reject calls without an active session", "[sdk][profile-ui]") {
  using rex::X_STATUS;
  using rex::X_RESULT;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  PPCContext ctx{};
  auto* base = rt.memory()->virtual_membase();
  ctx.r3.u64 = 0; ctx.r4.u64 = 1;
  __imp__XamEnableSystemAppInput(ctx, base);
  CHECK(ctx.r3.u32 == 1627);
  ctx.r3.u64 = 0;
  __imp__XamAppUnloadSelf(ctx, base);
  CHECK(ctx.r3.u32 == 1627);
  const auto stroke = rt.memory()->SystemHeapAlloc(8);
  ctx.r3.u64 = 0; ctx.r4.u64 = stroke;
  __imp__XamInputGetKeystrokeHud(ctx, base);
  CHECK(ctx.r3.u32 == X_ERROR_EMPTY);
  rt.memory()->SystemHeapFree(stroke);
}

TEST_CASE("System UI sessions isolate input and defer closure until the host releases ownership", "[sdk][profile-ui]") {
  using rex::X_STATUS;
  using rex::X_RESULT;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam);
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x9010D8A0, 13}) == 0);
  CHECK_FALSE(xam->BeginSystemUi(0x2000, 0xFF, 0));
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 4));
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  CHECK_FALSE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  CHECK_FALSE(xam->SetSystemUiVisible(0x2000));
  REQUIRE(xam->SetSystemUiVisible(0x1000));
  PPCContext ctx{};
  auto* base = rt.memory()->virtual_membase();
  __imp__XamIsUIActive(ctx, base);
  CHECK(ctx.r3.u32 == 1);
  ctx.r3.u64 = 1; ctx.r4.u64 = 1;
  __imp__XamEnableSystemAppInput(ctx, base);
  CHECK(ctx.r3.u32 == 1627);
  ctx.r3.u64 = 0; ctx.r4.u64 = 1;
  __imp__XamEnableSystemAppInput(ctx, base);
  CHECK(ctx.r3.u32 == 0);
  CHECK(xam->GetSystemUiSession().input_enabled);
  const auto state = rt.memory()->SystemHeapAlloc(sizeof(rex::input::X_INPUT_STATE));
  std::memset(base + state, 0xA5, sizeof(rex::input::X_INPUT_STATE));
  ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = state;
  __imp__XamInputGetState(ctx, base);
  CHECK(ctx.r3.u32 == 0);
  CHECK(std::all_of(base + state, base + state + sizeof(rex::input::X_INPUT_STATE),
                    [](uint8_t byte) { return byte == 0; }));
  rt.memory()->SystemHeapFree(state);
  const auto before = uint32_t(rex::chrono::Clock::QueryGuestUptimeMillis());
  ctx.r3.u64 = 0xDEADC0DE;
  __imp__GetTickCount(ctx, base);
  const auto after = uint32_t(rex::chrono::Clock::QueryGuestUptimeMillis());
  CHECK(uint32_t(ctx.r3.u32 - before) <= uint32_t(after - before));
  const auto stroke = rt.memory()->SystemHeapAlloc(8);
  std::memset(base + stroke, 0xA5, 8);
  ctx.r3.u64 = 0; ctx.r4.u64 = 1; ctx.r5.u64 = stroke;
  __imp__XamInputGetKeystroke(ctx, base);
  CHECK(ctx.r3.u32 == X_ERROR_EMPTY); // foreground app cannot drain the HUD queue
  CHECK(base[stroke] == 0xA5);
  ctx.r3.u64 = 1; ctx.r4.u64 = stroke;
  __imp__XamInputGetKeystrokeHud(ctx, base);
  CHECK(ctx.r3.u32 == X_ERROR_EMPTY); // another user cannot consume this session
  ctx.r3.u64 = 1;
  __imp__XamAppUnloadStack(ctx, base);
  CHECK(ctx.r3.u32 == 87);
  CHECK_FALSE(xam->GetSystemUiSession().close_requested);
  __imp__XamAppUnloadSelf(ctx, base);
  CHECK(ctx.r3.u32 == 0);
  CHECK(xam->GetSystemUiSession().close_requested);
  CHECK(xam->GetSystemUiSession().visible);
  CHECK_FALSE(xam->EndSystemUi(0x2000));
  REQUIRE(xam->EndSystemUi(0x1000));
  __imp__XamIsUIActive(ctx, base);
  CHECK(ctx.r3.u32 == 0);
  CHECK_FALSE(xam->GetSystemUiSession().input_enabled);
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  CHECK_FALSE(xam->GetSystemUiSession().close_requested);
  CHECK_FALSE(xam->GetSystemUiSession().visible);
  CHECK(xam->EndSystemUi(0x1000));
  rt.memory()->SystemHeapFree(stroke);
}

namespace {
class SystemUiProbe final : public rex::system::xam::App {
 public:
  explicit SystemUiProbe(rex::system::KernelState* state) : App(state, 0xFE) {}
  rex::X_HRESULT DispatchMessageSync(uint32_t message, uint32_t buffer,
                                   uint32_t length) override {
    ++calls;
    last_message = message;
    words.clear();
    for (uint32_t i = 0; i < length / 4; ++i)
      words.push_back(rex::memory::load_and_swap<uint32_t>(memory_->TranslateVirtual(buffer + i * 4)));
    return result;
  }
  unsigned calls = 0;
  uint32_t last_message = 0;
  std::vector<uint32_t> words;
  rex::X_HRESULT result = static_cast<rex::X_HRESULT>(0x80004005u);
};
}

TEST_CASE("Profile UI requests reach the system application without invented signin notifications",
          "[sdk][profile-ui]") {
  using rex::X_STATUS;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true; // Install only our app probe, not the real FE application.
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto app = std::make_unique<SystemUiProbe>(rt.kernel_state());
  auto* probe = app.get();
  rt.kernel_state()->app_manager()->RegisterApp(std::move(app));
  auto listener = rex::system::object_ref<rex::system::XNotifyListener>(
      new rex::system::XNotifyListener(rt.kernel_state()));
  listener->Initialize(1, 10);
  uint32_t id = 0, param = 0;
  while (listener->DequeueNotification(&id, &param)) {}
  PPCContext ctx{};
  ctx.r3.u64 = 1; ctx.r4.u64 = 2;
  __imp__XamShowSigninUI(ctx, rt.memory()->virtual_membase());
  CHECK(ctx.r3.u32 == 1627); // Original wrapper maps failed UI dispatch to ERROR_FUNCTION_FAILED.
  CHECK(probe->calls == 1);
  CHECK(probe->last_message == 0x21001);
  CHECK(probe->words == std::vector<uint32_t>{0, 0xFF, 1, 2, 0});
  CHECK_FALSE(listener->DequeueNotification(&id, &param));
  ctx.r3.u64 = 0;
  __imp__XamShowGamerCardUI(ctx, rt.memory()->virtual_membase());
  CHECK(ctx.r3.u32 == 1627);
  CHECK(probe->calls == 2);
  CHECK(probe->last_message == 0x21008);
  CHECK(probe->words == std::vector<uint32_t>(544 / 4, 0));
  CHECK_FALSE(listener->DequeueNotification(&id, &param));
}

TEST_CASE("Sign-in request propagates host outcomes without changing the user", "[sdk][signin]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  auto listener = rex::system::object_ref<rex::system::XNotifyListener>(
      new rex::system::XNotifyListener(rt.kernel_state()));
  listener->Initialize(1, 10);
  uint32_t id = 0, param = 0;
  while (listener->DequeueNotification(&id, &param)) {}
  for (const auto [host_result, expected] : {
       std::pair<uint32_t, uint32_t>{0x80004005, 1627}, {0x8007000E, 14}, {0, 0}}) {
    bool delivered = false;
    xam->system_ui_request = [&](uint32_t message, uint32_t request, uint32_t size) {
      delivered = true;
      CHECK(message == 0x21001);
      REQUIRE(size == 20);
      auto* words = rt.memory()->TranslateVirtual<rex::be<uint32_t>*>(request);
      CHECK(uint32_t(words[0]) == 0);
      CHECK(uint32_t(words[1]) == 0xFF);
      CHECK(uint32_t(words[2]) == 1);
      CHECK(uint32_t(words[3]) == 2);
      CHECK(uint32_t(words[4]) == 0);
      return static_cast<rex::X_HRESULT>(host_result);
    };
    PPCContext ctx{};
    ctx.r3.u64 = 1; ctx.r4.u64 = 2;
    __imp__XamShowSigninUI(ctx, rt.memory()->virtual_membase());
    CHECK(delivered);
    CHECK(ctx.r3.u32 == expected);
    CHECK_FALSE(listener->DequeueNotification(&id, &param));
  }
  xam->system_ui_request = {};
}

TEST_CASE("System UI registration preserves module ownership and rejects occupied slots", "[sdk][profile-ui]") {
  using rex::X_STATUS;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  rex::kernel::xam::XamModule xam(&rt, rt.kernel_state());
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
  using rex::X_STATUS;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* memory = rt.memory();
  const auto output = memory->SystemHeapAlloc(4);
  REQUIRE(output != 0);
  auto* result = memory->TranslateVirtual<rex::be<uint32_t>*>(output);
  *result = 0;
  PPCContext ctx{};
  ctx.r3.u64 = 0x20000000; ctx.r4.u64 = 0; ctx.r5.u64 = 808; ctx.r6.u64 = output;
  __imp__XamAllocEx(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 0);
  REQUIRE(uint32_t(*result) != 0);
  auto allocated = uint32_t(*result);
  // Exercise the entire constructor allocation, including its last member.
  std::memset(memory->TranslateVirtual(allocated), 0xA5, 808);
  CHECK(memory->TranslateVirtual(allocated)[807] == 0xA5);
  ctx.r3.u64 = allocated;
  __imp__XamFree(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 0);
  *result = 0xDEADBEEF;
  ctx.r3.u64 = 0x20000000; ctx.r4.u64 = 0x40000000; ctx.r5.u64 = 808; ctx.r6.u64 = output;
  __imp__XamAllocEx(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 0x80004001); // Unimplemented allocation class must fail explicitly.
  CHECK(uint32_t(*result) == 0);
  memory->SystemHeapFree(output);
}

#include <chrono>
#include <fstream>
#include <rex/kernel/init.h>
#include <rex/system/user_module.h>
#include "synthetic_xex.h"
namespace {
void ProfileModuleProbe(PPCContext&, uint8_t*) {}
void RegisterProfileProbe(rex::runtime::IModuleRegistrar* registrar) {
  registrar->SetFunction(0x82010000, ProfileModuleProbe);
}
}
TEST_CASE("Unloading a system module clears code registered by a game path alias", "[sdk][profile-ui]") {
  using rex::X_STATUS;
  const auto root = std::filesystem::temp_directory_path() /
      ("x360n-profile-module-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "system");
  struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
  const auto bytes = x360n::test::BuildXex({}, nullptr, 0, x360n::test::Entry::kReturnZero);
  { std::ofstream file(root / "system" / "profile.xex", std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  rex::Runtime rt(root, root);
  rex::RuntimeConfig config{}; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* state = rt.kernel_state();
  state->RegisterRecompiledModule("profile.dll", "game:\\system\\profile.xex", "");
  auto module = state->LoadUserModule("game:\\system\\profile.xex", false);
  REQUIRE(module);
  const auto registration = state->FindRecompiledModule("game:\\system\\profile.xex");
  REQUIRE(registration);
  // Stand in for the native DLL registrar, using the same public registration
  // path. The loaded XEX path has already been resolved through the VFS.
  REQUIRE(module->path() != registration->guest_path);
  module->set_recompiled_module_key(registration->guest_path);
  auto* dispatcher = rt.function_dispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
  dispatcher->RegisterModule(registration->guest_path, 0x82010000, RegisterProfileProbe);
  REQUIRE(dispatcher->GetFunction(0x82010000) == ProfileModuleProbe);
  state->UnloadUserModule(module, false);
  CHECK(dispatcher->GetFunction(0x82010000) == nullptr);
  // A second load must be able to install a table at the same image address.
  CHECK(dispatcher->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
}

extern "C" REX_FUNC(__imp__XMemAlloc);
extern "C" REX_FUNC(__imp__XMemFree);
TEST_CASE("XMem virtual allocation owns writable aligned memory and rejects unsupported physical classes", "[sdk][profile-ui]") {
  using rex::X_STATUS;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  PPCContext ctx{};
  auto* memory = rt.memory();
  ctx.r3.u64 = 68; ctx.r4.u64 = 0x24800000;
  __imp__XMemAlloc(ctx, memory->virtual_membase());
  const auto pointer = ctx.r3.u32;
  REQUIRE(pointer != 0);
  CHECK((pointer & 0x1F) == 0);
  std::memset(memory->TranslateVirtual(pointer), 0xA5, 68);
  CHECK(memory->TranslateVirtual(pointer)[67] == 0xA5);
  uint32_t size = 0;
  REQUIRE(memory->LookupHeap(pointer)->QuerySize(pointer, &size));
  CHECK(size >= 68);
  ctx.r3.u64 = pointer; ctx.r4.u64 = 0; // Free identifies the block from its pointer.
  __imp__XMemFree(ctx, memory->virtual_membase());
  REQUIRE(memory->LookupHeap(pointer)->QuerySize(pointer, &size));
  CHECK(size == 0);
  for (const auto attributes : {0x608A2002u, 0x61820005u}) {
  ctx.r3.u64 = 384; ctx.r4.u64 = attributes;
  __imp__XMemAlloc(ctx, memory->virtual_membase());
  const auto text_pointer = ctx.r3.u32;
  REQUIRE(text_pointer != 0);
  CHECK(std::all_of(memory->TranslateVirtual(text_pointer), memory->TranslateVirtual(text_pointer) + 384,
                    [](uint8_t byte) { return byte == 0; }));
  ctx.r3.u64 = text_pointer; ctx.r4.u64 = 0;
  __imp__XMemFree(ctx, memory->virtual_membase());
  REQUIRE(memory->LookupHeap(text_pointer)->QuerySize(text_pointer, &size));
  CHECK(size == 0);
  }
  ctx.r3.u64 = 68; ctx.r4.u64 = 0xA4800000; // Physical class is not supported here.
  __imp__XMemAlloc(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 0);
}

TEST_CASE("HUD allocation uses the system pool and remains owned until XamFree", "[sdk][profile-ui][hud-allocation]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto* memory = rt.memory();
  const auto output = memory->SystemHeapAlloc(4);
  REQUIRE(output != 0);
  auto* result = memory->TranslateVirtual<rex::be<uint32_t>*>(output);
  *result = 0xDEADBEEF;
  PPCContext ctx{};
  ctx.r3.u64 = 0x20160000; ctx.r4.u64 = 0x01000000; ctx.r5.u64 = 4096; ctx.r6.u64 = output;
  __imp__XamAllocEx(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == 0);
  REQUIRE(uint32_t(*result) != 0);
  const auto pointer = uint32_t(*result);
  std::memset(memory->TranslateVirtual(pointer), 0xA5, 4096);
  CHECK(memory->TranslateVirtual(pointer)[4095] == 0xA5);
  uint32_t size = 0;
  REQUIRE(memory->LookupHeap(pointer)->QuerySize(pointer, &size));
  CHECK(size >= 4096);
  ctx.r3.u64 = pointer;
  __imp__XamFree(ctx, memory->virtual_membase());
  REQUIRE(memory->LookupHeap(pointer)->QuerySize(pointer, &size));
  CHECK(size == 0);
  memory->SystemHeapFree(output);
}
