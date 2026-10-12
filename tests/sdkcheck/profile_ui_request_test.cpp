#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <rex/runtime.h>
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/app_manager.h>
#include <rex/system/xnotifylistener.h>

extern "C" REX_FUNC(__imp__XamShowSigninUI);
extern "C" REX_FUNC(__imp__XamShowGamerCardUI);

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
