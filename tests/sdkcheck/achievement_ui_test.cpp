#include <catch2/catch_test_macros.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <thread>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

extern "C" REX_FUNC(__imp__XamShowAchievementsUI);
extern "C" REX_FUNC(__imp__XamShowAchievementsUIEx);
extern "C" REX_FUNC(__imp__XamShowAchievementDetailsUI);
extern "C" REX_FUNC(__imp__XNotifyQueueUI);
extern "C" REX_FUNC(__imp__XNotifyQueueUIEx);

TEST_CASE("Achievement notification callback can be cleared during worker delivery", "[sdk][achievement-ui]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  std::promise<void> entered, release;
  auto entered_signal = entered.get_future();
  auto release_signal = release.get_future();
  std::atomic<uint32_t> calls{0};
  std::atomic<bool> timed_out{false};
  std::array<uint64_t, 4> captured{};
  xam->SetAchievementUnlockedCallback([&](uint64_t xuid, uint32_t user, uint32_t title, uint32_t achievement) {
    ++calls;
    captured = {xuid, user, title, achievement};
    entered.set_value();
    // Bound a broken implementation's mutex deadlock without leaving a worker.
    timed_out = release_signal.wait_for(std::chrono::seconds(5)) != std::future_status::ready;
  });
  std::jthread worker([&] { xam->NotifyAchievementUnlocked(0xE00000123456789A, 2, 0x12345678, 77); });
  const bool started = entered_signal.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
  if (started) {
    xam->SetAchievementUnlockedCallback({});
    xam->NotifyAchievementUnlocked(1, 0, 2, 3);
  }
  release.set_value();
  worker.join();
  REQUIRE(started);
  CHECK_FALSE(timed_out.load());
  CHECK(calls == 1);
  CHECK((captured == std::array<uint64_t, 4>{0xE00000123456789A, 2, 0x12345678, 77}));
  // Reentrant removal is allowed too: host work executes outside the mutex.
  xam->SetAchievementUnlockedCallback([&](uint64_t, uint32_t, uint32_t, uint32_t) {
    ++calls;
    xam->SetAchievementUnlockedCallback({});
  });
  xam->NotifyAchievementUnlocked(1, 0, 2, 3);
  xam->NotifyAchievementUnlocked(1, 0, 2, 3);
  CHECK(calls == 2);
}

TEST_CASE("Achievement UI preserves original GamerProfile modes and PPC XUID", "[sdk][achievement-ui]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(rt.function_dispatcher()->InitializeFunctionTable(0x82000000, 0x1000, 0x82000000, 0x10000, true, true));
  std::array<PPCFunc*, 3> resolved{};
  constexpr std::array<uint16_t, 3> ordinals{0x02C5, 0x03C2, 0x02E0};
  for (size_t i = 0; i < ordinals.size(); ++i) {
    const auto thunk = xam->GetProcAddressByOrdinal(ordinals[i]);
    REQUIRE(thunk);
    resolved[i] = rt.function_dispatcher()->GetFunction(thunk);
    REQUIRE(resolved[i]);
  }
  std::array<uint32_t, 136> captured{};
  uint32_t calls = 0, status = 0;
  xam->system_ui_request = [&](uint32_t message, uint32_t request, uint32_t size) {
    CHECK(message == 0x21008); REQUIRE(size == 544);
    auto* words = rt.memory()->TranslateVirtual<rex::be<uint32_t>*>(request);
    for (size_t i = 0; i < captured.size(); ++i) captured[i] = words[i];
    ++calls;
    return status;
  };
  PPCContext ctx{};
  auto* base = rt.memory()->virtual_membase();
  ctx.r3.u64 = 0; ctx.r4.u64 = 0x12345678; ctx.r5.u64 = 0xE00000123456789A;
  resolved[1](ctx, base);
  CHECK(ctx.r3.u32 == 0); CHECK(calls == 1);
  CHECK(captured[0] == 0); CHECK(captured[1] == 2);
  CHECK(captured[2] == 0xE0000012); CHECK(captured[3] == 0x3456789A);
  CHECK(captured[4] == 0x12345678); CHECK(captured[5] == 0);
  for (size_t i = 6; i < captured.size(); ++i) CHECK(captured[i] == 0);
  ctx.r3.u64 = 2; ctx.r4.u64 = 0x87654321; ctx.r5.u64 = 0xFFFFFFFFFFFFFFFF;
  resolved[0](ctx, base);
  CHECK(ctx.r3.u32 == 0); CHECK(captured[0] == 2); CHECK(captured[1] == 2);
  CHECK(captured[2] == 0); CHECK(captured[3] == 0); CHECK(captured[4] == 0x87654321);
  ctx.r3.u64 = 1; ctx.r4.u64 = 0x11223344; ctx.r5.u64 = 77;
  resolved[2](ctx, base);
  CHECK(ctx.r3.u32 == 0); CHECK(captured[0] == 1); CHECK(captured[1] == 4);
  CHECK(captured[4] == 0x11223344); CHECK(captured[5] == 77);
  const auto before = calls;
  ctx.r3.u64 = 4;
  __imp__XamShowAchievementsUI(ctx, base);
  CHECK(ctx.r3.u32 == 87); CHECK(calls == before);
  status = 0x8007000E; ctx.r3.u64 = 0;
  __imp__XamShowAchievementsUI(ctx, base);
  CHECK(ctx.r3.u32 == 14);
  status = 0x80004005; ctx.r3.u64 = 0;
  __imp__XamShowAchievementsUI(ctx, base);
  CHECK(ctx.r3.u32 == 1627);
  xam->system_ui_request = {};
  ctx.r3.u64 = 0;
  __imp__XamShowAchievementsUI(ctx, base);
  CHECK(ctx.r3.u32 == 1627);
}

TEST_CASE("Notification UI routes the original request without a host overlay", "[sdk][achievement-ui]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(rt.function_dispatcher()->InitializeFunctionTable(0x82000000, 0x1000, 0x82000000, 0x10000, true, true));
  std::array<PPCFunc*, 2> resolved{};
  constexpr std::array<uint16_t, 2> ordinals{0x0290, 0x0298};
  for (size_t i = 0; i < ordinals.size(); ++i) {
    const auto thunk = xam->GetProcAddressByOrdinal(ordinals[i]);
    REQUIRE(thunk);
    resolved[i] = rt.function_dispatcher()->GetFunction(thunk);
    REQUIRE(resolved[i]);
  }
  std::array<uint32_t, 10> captured{};
  uint32_t calls = 0;
  xam->system_ui_request = [&](uint32_t message, uint32_t request, uint32_t size) {
    CHECK(message == 0x21013); REQUIRE(size == 40);
    auto* words = rt.memory()->TranslateVirtual<rex::be<uint32_t>*>(request);
    for (size_t i = 0; i < captured.size(); ++i) captured[i] = words[i];
    ++calls;
    return uint32_t(0);
  };
  const auto text = rt.memory()->SystemHeapAlloc(16);
  REQUIRE(text);
  std::memset(rt.memory()->TranslateVirtual(text), 0, 16);
  PPCContext ctx{};
  auto* base = rt.memory()->virtual_membase();
  ctx.r3.u64 = 27; ctx.r4.u64 = 0; ctx.r5.u64 = 2; ctx.r6.u64 = text; ctx.r7.u64 = 0x12345678;
  resolved[0](ctx, base);
  CHECK(ctx.r3.u32 == 0); CHECK(calls == 1);
  CHECK((captured == std::array<uint32_t, 10>{27, 0, 2, text, 0, 0, 0, 0, 0, 0x12345678}));
  ctx.r3.u64 = 27; ctx.r4.u64 = 0xFF; ctx.r5.u64 = 1; ctx.r6.u64 = text;
  ctx.r7.u64 = 71; ctx.r8.u64 = 82; ctx.r9.u64 = 93; ctx.r10.u64 = 0x1020304050607080;
  resolved[1](ctx, base);
  CHECK(ctx.r3.u32 == 0); CHECK(calls == 2);
  CHECK((captured == std::array<uint32_t, 10>{27, 0xFF, 1, text, 82, 71, 93, 0, 0x10203040, 0x50607080}));
  ctx.r3.u64 = 27; ctx.r4.u64 = 4;
  __imp__XNotifyQueueUI(ctx, base);
  CHECK(ctx.r3.u32 == 0x80070057); CHECK(calls == 2);
  ctx.r3.u64 = 27; ctx.r4.u64 = 0; ctx.r5.u64 = 4;
  __imp__XNotifyQueueUI(ctx, base);
  CHECK(ctx.r3.u32 == 0x80070057); CHECK(calls == 2);
  ctx.r3.u64 = 27; ctx.r4.u64 = 0; ctx.r5.u64 = 2; ctx.r6.u64 = text + 1;
  __imp__XNotifyQueueUI(ctx, base);
  CHECK(ctx.r3.u32 == 0x80070057); CHECK(calls == 2);
  ctx.r3.u64 = 27; ctx.r6.u64 = 0xFFFE0000;
  __imp__XNotifyQueueUI(ctx, base);
  CHECK(ctx.r3.u32 == 0x80070057); CHECK(calls == 2);
  xam->system_ui_request = {};
  ctx.r3.u64 = 27; ctx.r4.u64 = 0; ctx.r5.u64 = 2; ctx.r6.u64 = text;
  __imp__XNotifyQueueUI(ctx, base);
  CHECK(static_cast<int32_t>(ctx.r3.u32) < 0);
  rt.memory()->SystemHeapFree(text);
}
