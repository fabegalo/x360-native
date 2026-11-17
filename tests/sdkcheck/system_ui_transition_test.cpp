#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include <rex/kernel/function_dispatcher.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>
#include "../../titles/dash_17559/src/system_ui_transition.h"
#include <vector>

namespace {
struct ClosingProbe {
  uint32_t hud = 0;
  uint32_t result = 0x8000000A;
  bool buffer_ready = true;
  std::vector<char> calls;
};
ClosingProbe* probe;
void Open(PPCContext& ctx, uint8_t*) {
  CHECK(ctx.r3.u32 == probe->hud);
  CHECK(ctx.r4.u32 == 1); // XamShowGuideUI's original mode, not a host animation.
  probe->calls.push_back('O');
  ctx.r3.u64 = 0x8000000A;
}
void Close(PPCContext& ctx, uint8_t*) {
  CHECK(ctx.r3.u32 == probe->hud);
  probe->calls.push_back('C');
  ctx.r3.u64 = probe->result;
}
void Ready(PPCContext& ctx, uint8_t*) {
  probe->calls.push_back('Q');
  ctx.r3.u64 = probe->buffer_ready;
}
void Compose(PPCContext& ctx, uint8_t*) {
  CHECK(ctx.r3.u32 == 0x12340000);
  probe->calls.push_back('R');
}
}

TEST_CASE("Retail 17559 closing keeps composing until the original HUD completes",
          "[sdk][guide-transition]") {
  rex::test::KernelFixture fixture;
  auto& rt = fixture.GetRuntime();
  auto* memory = &rex::kernel::GetAddressSpace(rt);
  const auto flags = rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit;
  const auto rw = rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite;
  for (const auto address : {0x815F0000u, 0x81AA0000u})
    REQUIRE(memory->LookupHeap(address)->AllocFixed(address, 0x10000, 0x10000, flags, rw));
  auto put = [&](uint32_t address, uint32_t value) {
    rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(address), value);
  };
  auto read = [&](uint32_t address) {
    return rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
  };
  auto* dispatcher = &rex::kernel::GetFunctionDispatcher(rt);
  REQUIRE(dispatcher->InitializeFunctionTable(0x81600000, 0x300000, 0x81600000, 0x300000, false, true));
  REQUIRE(dispatcher->SetFunction(0x81695880, Close));
  REQUIRE(dispatcher->SetFunction(0x81695868, Open));
  REQUIRE(dispatcher->SetFunction(0x81842110, Ready));
  REQUIRE(dispatcher->SetFunction(0x816CCF00, Compose));
  ClosingProbe state;
  state.hud = memory->SystemHeapAlloc(128);
  REQUIRE(state.hud);
  probe = &state;
  auto* xam = &REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI();
  REQUIRE(xam->RegisterSystemApp({0x1000, 0xFF, 0x913E5930, 0}) == 0);
  REQUIRE(xam->BeginSystemUi(0x1000, 0xFF, 0));
  REQUIRE(xam->SetSystemUiVisible(0x1000));
  REQUIRE(xam->RequestSystemUiClose());
  put(0x815F86F8, 0x3F800000); // original hidden fade = 1
  put(0x81AA21B4, 0x3F000000); // shown target/current = 0.5
  put(0x81AA21B8, 0x3F000000);
  put(0x81AA2188, 1);
  put(0x81AA1B18, 0x12340000);
  bool keep_fade = false;
  SECTION("original completion after multiple frames") {}
  SECTION("GPU backpressure retains the transition without publishing") { state.buffer_ready = false; }
  SECTION("original host flag retains its fade target") { keep_fade = true; }
  SECTION("an original failure terminates rather than waiting forever") { state.result = 0x80004005; }
  SECTION("absent or already hidden background completes immediately") { state.result = 0; }
  put(0x81AA21BC, keep_fade ? 1 : 0);
  fixture.RunOnGuestThread([&](rex::test::Caller&) {
    dash17559::SystemUiCloseTransition transition;
    CHECK_FALSE(transition.started());
    auto* thread = rex::kernel::ThreadState::Get();
    CHECK(dash17559::BeginGuideBackground(rt, thread, state.hud) == 0x8000000A);
    CHECK(state.calls == std::vector<char>{'O'});
    CHECK(read(0x81AA21B8) == 0x3F000000);
    state.calls.clear();
    const bool pending = state.result == 0x8000000A;
    CHECK(transition.Advance(rt, thread, state.hud) == pending);
    CHECK(transition.started());
    CHECK(read(0x81AA21B4) == (keep_fade ? 0x3F000000 : 0x3F800000));
    CHECK(read(0x81AA21B8) == 0x3F000000); // Never snap the current fade.
    CHECK(read(0x81AA2188) == 1);
    CHECK(xam->GetSystemUiSession().visible);
    if (pending) {
      CHECK(state.calls == (state.buffer_ready ? std::vector<char>{'C', 'Q', 'R'} : std::vector<char>{'C', 'Q'}));
      // A later VdSwap repeats the original status call; it does not use a
      // host timer/frame count or restart the fade as new input arrives.
      put(0x81AA21B8, 0x3F400000); // 0.75, advanced by the guest compositor
      state.buffer_ready = true;
      CHECK(transition.Advance(rt, thread, state.hud));
      CHECK(read(0x81AA21B8) == 0x3F400000);
      CHECK(xam->GetSystemUiSession().visible);
      state.calls.clear();
      state.result = 0;
      CHECK_FALSE(transition.Advance(rt, thread, state.hud));
      CHECK(state.calls == std::vector<char>{'C'});
    } else {
      CHECK(state.calls == std::vector<char>{'C'});
    }
    // The host may now release the app/session and publish the title frame.
    CHECK(xam->EndSystemUi(0x1000));
  });
  CHECK_FALSE(xam->GetSystemUiSession().visible);
  probe = nullptr;
  memory->SystemHeapFree(state.hud);
}
