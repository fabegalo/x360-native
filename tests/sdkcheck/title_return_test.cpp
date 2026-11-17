#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include "kernel/title_fixture.h"
#include "kernel/test_access.h"
#include "kernel/title_state.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/title_paths.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/loader_state.h>
#include "xam/xam_state.h"
#include <rex/xam/user_profile.h>
#include <rex/platform/file.h>
#include "synthetic_xex.h"
#include "../../titles/common/launch_handoff.h"
#include "../../titles/common/launch_request.h"
using namespace rex::kernel;

extern "C" REX_FUNC(__imp__XamLoaderSetLaunchData);
extern "C" REX_FUNC(__imp__XamLoaderLaunchTitle);
extern "C" REX_FUNC(__imp__XamLoaderTerminateTitle);
extern "C" REX_FUNC(__imp__XamLoaderRebootToDash);
extern "C" REX_FUNC(__imp__ExRegisterTitleTerminateNotification);

namespace {
struct LaunchScope {
  struct Saved {rex::SettingBase* setting;std::string text;rex::SettingLayer layer;};
  std::vector<Saved> saved;
  LaunchScope() {
    REQUIRE(x360n::RegisterLaunchSettings(REX_SETTINGS()));
    auto& settings=x360n::GetLaunchSettings();
    for(rex::SettingBase* setting:{&settings.handoff_in,&settings.handoff_out,&settings.profile_root})
      saved.push_back({setting,setting->GetText(),setting->GetLayer()});
  }
  ~LaunchScope() {
    for(const auto& entry:saved) {CHECK(entry.setting->Reset());
      if(entry.layer!=rex::SettingLayer::kDefault)CHECK(entry.setting->SetText(entry.layer,entry.text));}
    REX_SETTINGS().Unregister(x360n::GetLaunchSettings());
  }
};
}
namespace {
std::vector<uint32_t> termination_calls;
uint32_t termination_registrations;
bool callbacks_before_thread_shutdown;
void InitialThreadEntry(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = 0; }

TEST_CASE("initial title processor does not depend on preceding host workers", "[sdk][title-return][main-cpu]") {
  x360n::test::Workdir work("x360n_title_return", "main-cpu");
  std::vector<x360n::test::Placed> placed;
  work.write("default.xex", x360n::test::BuildXex({}, &placed, 0, x360n::test::Entry::kReturnZero));
  rex::test::ReleaseTestMemory();
  rex::test::KernelHost host(work.dir);
  TitleRuntime rt(rex::ppc::PPCImage{});
  REQUIRE(rt.GetTitlePaths().game_root.Set(work.dir));
  REQUIRE(rt.GetTitlePaths().entry.Set("default.xex")); REQUIRE(rt.GetTitlePaths().imports.Set(""));
  rex::ScopeGuard unload([&] {rt.Unload(); CHECK(rt.GetTitlePaths().game_root.Reset());
    CHECK(rt.GetTitlePaths().entry.Reset()); CHECK(rt.GetTitlePaths().imports.Reset());});
  REQUIRE(rt.Load());
  REQUIRE(GetFunctionDispatcher(rt).InitializeFunctionTable(0x82010000,0x1000,0x82000000,0x20000));
  REQUIRE(GetFunctionDispatcher(rt).SetFunction(0x82010000,InitialThreadEntry));
  for(unsigned i=0;i<6;++i) {
    INFO("preceding host worker iteration " << i);
    auto worker=rt.GetThreadRegistry().Launch(std::make_unique<KernelThread>(rt,128*1024,
        i%2?0x10000000:0, [] {return 0;})); REQUIRE(worker);
    uint64_t timeout=uint64_t(-50'000'000ll);
    REQUIRE(rt.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*worker,0,0,0,&timeout)==0);
    CloseOwned(rt.GetObjectTable(),*worker);
    auto handle=StateOf(rt).PrepareModuleLaunch(); REQUIRE(handle);
    auto main=rt.GetThreadRegistry().FindByHandle(handle); REQUIRE(main);
    CHECK(main->GetActiveCPU()==0);
    const auto* ctx=main->GetThreadState()->GetContext();
    CHECK(rt.GetMembase().GetBase()[ctx->r13.u32+0x10C]==0);
    REQUIRE(main->Resume()==0);
    REQUIRE(rt.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(handle,0,0,0,&timeout)==0);
    CloseOwned(rt.GetObjectTable(),handle);
  }
}

void TerminationCallback(PPCContext& ctx, uint8_t* base) {
  const auto address = ctx.r3.u32;
  termination_calls.push_back(address);
  callbacks_before_thread_shutdown &= !rex::kernel::StateOf(*REX_RUNTIME()).IsTerminatingTitle();
  // Original D3D callback 921B6160 removes its own registration using r3.
  ctx.r4.u64 = 0;
  __imp__ExRegisterTitleTerminateNotification(ctx, base);
  if (address == termination_registrations + 32) {
    // Removing a later registration must prevent its dispatch; routine is shared.
    ctx.r3.u64 = termination_registrations + 16;
    ctx.r4.u64 = 0;
    __imp__ExRegisterTitleTerminateNotification(ctx, base);
  }
}

TEST_CASE("original HUD reboot preserves its packed dashboard context", "[sdk][title-return]") {
  x360n::test::Workdir work("x360n_title_return", "hud-reboot");
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, work.dir);
  auto& rt=fixture.GetRuntime();
  auto& loader = REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState();
  uint32_t reason = 0;
  SECTION("Guide home") {}
  SECTION("special dashboard launch reason") { reason = 72; }
  SECTION("invalid context leaves title running") {
    PPCContext ctx{};
    for (auto address : {0u, 0x7FFFFFF0u, 0xFFFFFFF0u}) {
      ctx.r3.u64 = address;
      __imp__XamLoaderRebootToDash(ctx, rt.GetMembase().GetBase());
      CHECK(ctx.r3.u32 == 87);
      CHECK_FALSE(loader.launch_data_present);
      CHECK_FALSE(StateOf(rt).IsTerminatingTitle());
    }
    return;
  }
  std::vector<uint8_t> bytes(1020);
  for (size_t i = 12; i < bytes.size(); ++i) bytes[i] = uint8_t(i);
  rex::memory::store_and_swap<uint32_t>(bytes.data() + 4, reason);
  rex::memory::store_and_swap<uint32_t>(bytes.data() + 8, 0xFF);
  const auto ptr = rt.GetGuestHeap().SystemHeapAlloc(bytes.size());
  REQUIRE(ptr != 0);
  std::memcpy((rt.GetMembase().GetBase()+ptr), bytes.data(), bytes.size());
  bool returned = false;
  fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& caller) {
    auto* ctx=&caller.GetContext();
    ctx->r3.u64 = ptr;
    __imp__XamLoaderRebootToDash(*ctx, rt.GetMembase().GetBase());
    returned = true;
  });
  CHECK_FALSE(returned);
  CHECK(loader.exit_request == rex::xam::XAMLoaderState::ExitRequest::Dashboard);
  CHECK(loader.launch_path.empty());
  CHECK(loader.launch_data_present);
  CHECK(loader.launch_data == bytes);
  CHECK(loader.launch_flags == (reason == 72 ? 0x100000 : 0));
}
}

TEST_CASE("session identity follows the dashboard hard drive and its session", "[sdk][title-return]") {
  x360n::test::Workdir work("x360n_title_return", "profile-source");
  const auto platform = work.dir / "platform";
  const auto game = work.dir / "game";
  std::filesystem::create_directories(platform);
  std::filesystem::create_directories(game);
  rex::xam::UserProfile owner;
  REQUIRE(owner.InitializeLocal(platform));
  uint64_t owner_id = 0;
  REQUIRE(owner.CreateLocal("Session", &owner_id) == 0);
  REQUIRE(owner.SetLocalSession(owner_id) == 0);
  // As the supervisor does: one console hard drive for every title.
  auto& content_root=GetTitlePaths().content_root;
  const auto prior=content_root.GetText();const auto prior_layer=content_root.GetLayer();
  rex::ScopeGuard restore_root([&] {CHECK(content_root.Reset());
    if(prior_layer!=rex::SettingLayer::kDefault)CHECK(content_root.SetText(prior_layer,prior));});
  rex::test::KernelFixture fixture([&](const auto&) {REQUIRE(content_root.Set(platform));}, {}, {}, {}, {}, game);
  auto& rt=fixture.GetRuntime(); LaunchScope launch_settings;
  auto* profile=&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetUserProfile();
  REQUIRE(profile->identity().xuid==owner_id); CHECK(profile->identity().signin_state==0);
  REQUIRE(x360n::GetLaunchSettings().profile_root.Set(platform.string()));
  x360n::ImportLaunchData(rt);
  CHECK(profile->identity().xuid == owner_id);
  CHECK(profile->identity().signin_state == 1);
  CHECK(rex::xam::CurrentTitle().user_data_root == rex::platform::HostPath(game));
  REQUIRE(profile->SetLocalSession(0) == 0);
  REQUIRE(owner.InitializeLocal(platform));
  CHECK(owner.identity().signin_state == 0);
  // No profile copy is made in the title's own root.
  for (const auto& entry : std::filesystem::directory_iterator(game))
    CHECK(entry.path().filename().string().size() != 16);
}

TEST_CASE("title termination dispatches registered guest callbacks before stopping threads",
          "[sdk][title-return]") {
  x360n::test::Workdir work("x360n_title_return", "callbacks");
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, work.dir);
  auto& rt=fixture.GetRuntime();
  REQUIRE((&GetFunctionDispatcher(rt))->SetFunction(0x8F000100, TerminationCallback));
  termination_registrations = rt.GetGuestHeap().SystemHeapAlloc(64);
  REQUIRE(termination_registrations != 0);
  termination_calls.clear(); callbacks_before_thread_shutdown = true;
  auto* regs = reinterpret_cast<X_EX_TITLE_TERMINATE_REGISTRATION*>(rt.GetMembase().GetBase()+termination_registrations);
  for (unsigned i = 0; i != 3; ++i) {
    regs[i] = {}; regs[i].notification_routine = 0x8F000100; regs[i].priority = (i + 1) * 10;
  }
  bool returned = false;
  fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& caller) {
    auto* ctx=&caller.GetContext();
    for (unsigned i = 0; i != 3; ++i) {
      ctx->r3.u64 = termination_registrations + i * 16; ctx->r4.u64 = 1;
      __imp__ExRegisterTitleTerminateNotification(*ctx, rt.GetMembase().GetBase());
    }
    __imp__XamLoaderTerminateTitle(*ctx, rt.GetMembase().GetBase());
    returned = true;
  });
  CHECK_FALSE(returned);
  CHECK(termination_calls == std::vector<uint32_t>{termination_registrations + 32, termination_registrations});
  CHECK(callbacks_before_thread_shutdown);
}

TEST_CASE("original loader return preserves flags and launch data through host handoff",
          "[sdk][title-return]") {
  x360n::test::Workdir work("x360n_title_return", "loader");
  bool terminate = false;
  uint32_t flags = 0;
  SECTION("explicit return to dashboard") {}
  SECTION("arcade return flags are preserved for supervisor validation") { flags = 0x100; }
  SECTION("title termination is distinct from bare thread return") { terminate = true; }
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, work.dir);
  auto& rt=fixture.GetRuntime();
  LaunchScope launch_settings;
  const auto ptr = rt.GetGuestHeap().SystemHeapAlloc(16);
  const std::vector<uint8_t> bytes{0, 1, 0xFE, 0xFF, 0x42};
  std::memcpy((rt.GetMembase().GetBase()+ptr), bytes.data(), bytes.size());
  bool returned = false;
  fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& caller) {
    auto* ctx=&caller.GetContext();
    ctx->r3.u64 = ptr; ctx->r4.u64 = bytes.size();
    __imp__XamLoaderSetLaunchData(*ctx, rt.GetMembase().GetBase());
    ctx->r3.u64 = 0; ctx->r4.u64 = flags;
    if (terminate) __imp__XamLoaderTerminateTitle(*ctx, rt.GetMembase().GetBase());
    else __imp__XamLoaderLaunchTitle(*ctx, rt.GetMembase().GetBase());
    returned = true;
  });
  CHECK_FALSE(returned);
  auto& loader = REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState();
  CHECK(loader.launch_path.empty());
  CHECK(loader.launch_flags == flags);
  CHECK(loader.launch_data == bytes);
  auto file = work.dir / "return.bin";
  std::filesystem::remove(file);
  REQUIRE(x360n::GetLaunchSettings().handoff_out.Set(file.string()));
  x360n::ExportLaunchRequest(rt);
  REQUIRE(x360n::GetLaunchSettings().handoff_out.Set(std::string()));
  x360n::LaunchRequest request;
  REQUIRE(x360n::ReadLaunchRequest(file, request));
  CHECK(request.kind == (terminate ? x360n::LaunchRequest::TitleTerminate : x360n::LaunchRequest::Dashboard));
  CHECK(request.flags == flags);
  CHECK(request.data_present);
  CHECK(request.data == bytes);
  loader.launch_data.clear(); loader.launch_data_present = false;
  REQUIRE(x360n::GetLaunchSettings().handoff_in.Set(file.string()));
  x360n::ImportLaunchData(rt);
  REQUIRE(x360n::GetLaunchSettings().handoff_in.Set(std::string()));
  CHECK(x360n::IsDashboardReturn());
  CHECK(loader.launch_data == bytes);
  CHECK(loader.launch_data_present);
  CHECK(loader.launch_flags == flags);
}
