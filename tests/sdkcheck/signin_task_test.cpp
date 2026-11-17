#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include "kernel/kernel_fixture.h"
#include <rex/xam/task_context.h>
#include <stdexcept>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/function_dispatcher.h>
#include "kernel/test_access.h"
#include <rex/chrono.h>
#include "xam/xam_state.h"

using rex::test::Caller;
using rex::test::KernelFixture;
extern "C" REX_FUNC(__imp__XamTaskSchedule);
extern "C" REX_FUNC(__imp__XamTaskShouldExit);
extern "C" REX_FUNC(__imp__XamTaskCancel);
extern "C" REX_FUNC(__imp__XamTaskWaitOnCompletion);
extern "C" REX_FUNC(__imp__XamTaskCloseHandle);
extern "C" REX_FUNC(__imp__XamTaskGetCurrentTask);
extern "C" REX_FUNC(__imp__XamTaskGetStatus);
extern "C" REX_FUNC(__imp__XamTaskGetCompletionStatus);
extern "C" REX_FUNC(__imp__XamTaskGetAttributes);

namespace {
std::atomic<uint32_t> received_context, received_handle, worker_handle;
std::atomic<uint32_t> worker_system_task;
std::atomic<bool> entered, cancelled, finished;
void TaskCallback(PPCContext& ctx, uint8_t* base) {
  worker_system_task = rex::xam::CurrentSystemTask();
  received_context = ctx.r3.u32;
  worker_handle = rex::kernel::KernelThread::GetCallingThread()->GetHandle();
  __imp__XamTaskGetCurrentTask(ctx, base);
  received_handle = ctx.r3.u32;
  __imp__XamTaskCloseHandle(ctx, base); // GetCurrentTask owns a reference.
  entered = true;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  do {
    __imp__XamTaskShouldExit(ctx, base);
    if (ctx.r3.u32) { cancelled = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < deadline);
  // Cancellation is a request: Wait must also cover callback cleanup.
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  finished = true;
  ctx.r3.u64 = 0x81234567;
}

TEST_CASE("Host system tasks retain identity attributes and restore nested callers",
          "[sdk][signin-task][notification-task]") {
  KernelFixture fixture;
  uint32_t outside = ~0u, outer = 0, inner = 0, restored = 0, after = ~0u;
  uint32_t attributes = 0, attr_result = ~0u, invalid = 0, result = 0, null_output = 0;
  bool host_identity = false;
  bool outer_alive = false, inner_gone = false, exception_restored = false;
  fixture.RunOnGuestThread([&](Caller& caller) {
    const auto output = caller.Alloc(8);
    auto* base = caller.GetBase();
    auto& ctx = caller.GetContext();
    auto current = [&] {
      __imp__XamTaskGetCurrentTask(ctx, base);
      const auto handle = ctx.r3.u32;
      if (handle) __imp__XamTaskCloseHandle(ctx, base);
      return handle;
    };
    outside = current();
    result = rex::xam::RunSystemTask([&] {
      outer = current();
      ctx.r3.u64 = rex::xam::CurrentSystemTask();
      host_identity = ctx.r3.u32 == outer;
      __imp__XamTaskCloseHandle(ctx, base);
      outer_alive = rex::xam::IsRuntimeTask(outer);
      ctx.r3.u64 = outer; ctx.r4.u64 = output;
      __imp__XamTaskGetAttributes(ctx, base); attr_result = ctx.r3.u32;
      attributes = rex::memory::load_and_swap<uint32_t>(base + output);
      ctx.r3.u64 = outer; ctx.r4.u64 = 0;
      __imp__XamTaskGetAttributes(ctx, base); null_output = ctx.r3.u32;
      rex::xam::RunSystemTask([&] { inner = current(); return 0u; });
      inner_gone = !rex::xam::IsRuntimeTask(inner);
      restored = current();
      try {
        rex::xam::RunSystemTask([]() -> uint32_t { throw std::runtime_error("test"); });
      } catch (const std::runtime_error&) { exception_restored = current() == outer; }
      return 0x81234567u;
    });
    after = current();
    ctx.r3.u64 = outer; ctx.r4.u64 = output;
    __imp__XamTaskGetAttributes(ctx, base); invalid = ctx.r3.u32;
    CHECK_FALSE(rex::xam::IsRuntimeTask(outer));
  });
  CHECK(outside == 0); CHECK(after == 0);
  CHECK(outer != 0); CHECK(inner != 0); CHECK(inner != outer);
  CHECK(outer_alive); CHECK(inner_gone); CHECK(restored == outer);
  CHECK(exception_restored); CHECK(attr_result == 0);
  CHECK(attributes == 0x04000000);
  CHECK(host_identity); CHECK(null_output == 0x80070057);
  CHECK(invalid == 0x80070057); CHECK(result == 0x81234567);
}
}

TEST_CASE("Sign-in task cancellation joins callback before releasing its module",
          "[sdk][signin-task]") {
  received_context = received_handle = 0;
  entered = cancelled = finished = false;
  KernelFixture fixture;
  uint32_t address = 0;
  constexpr uint32_t callback_context = 0x10203040;
  const auto callback = rex::kernel::GetFunctionDispatcher(fixture.GetRuntime()).AllocateThunk(&TaskCallback, 0);
  REQUIRE(callback != 0);
  uint32_t scheduled = ~0u, pending = 0, joined = ~0u, result = 0, closed = ~0u, stale = 0, complete = 0;
  bool completed_at_join = false;
  fixture.RunOnGuestThread([&](Caller& caller) {
    address = caller.Alloc(16);
    auto* base = caller.GetBase();
    auto& ctx = caller.GetContext();
    ctx.r3.u64 = callback; ctx.r4.u64 = callback_context;
    ctx.r5.u64 = 0; ctx.r6.u64 = address;
    __imp__XamTaskSchedule(ctx, base);
    scheduled = ctx.r3.u32;
    auto handle = rex::memory::load_and_swap<uint32_t>(base + address);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!entered && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ctx.r3.u64 = handle; __imp__XamTaskGetStatus(ctx, base); pending = ctx.r3.u32;
    ctx.r3.u64 = handle; __imp__XamTaskCancel(ctx, base);
    ctx.r3.u64 = handle; __imp__XamTaskWaitOnCompletion(ctx, base); joined = ctx.r3.u32;
    completed_at_join = finished;
    ctx.r3.u64 = handle; __imp__XamTaskGetStatus(ctx, base); complete = ctx.r3.u32;
    ctx.r3.u64 = handle; __imp__XamTaskGetCompletionStatus(ctx, base); result = ctx.r3.u32;
    ctx.r3.u64 = handle; __imp__XamTaskCloseHandle(ctx, base); closed = ctx.r3.u32;
    ctx.r3.u64 = handle; __imp__XamTaskGetStatus(ctx, base); stale = ctx.r3.u32;
    CHECK(received_handle == caller.Load32(address));
  });
  CHECK(scheduled == 0);
  CHECK(received_context == callback_context);
  CHECK(worker_system_task == 0);
  CHECK(pending == 0);
  CHECK(cancelled);
  CHECK(joined == 0);
  CHECK(completed_at_join);
  CHECK(complete == 1);
  CHECK(result == 0x81234567);
  CHECK(closed == 0);
  CHECK(stale == 0x80070057u);
  CHECK_FALSE(fixture.GetRuntime().GetThreadRegistry().FindByHandle(worker_handle));
}

TEST_CASE("local task service queries preserve output ABI and offline account state", "[sdk][signin-task]") {
  KernelFixture fixture;
  fixture.RunOnGuestThread([&](Caller& caller) {
    const auto output = caller.Alloc(16);
    caller.Store32(output, 0xCDCDCDCD);
    caller.Store32(output + 4, 0xCDCDCDCD);
    CHECK(caller.XAM("XamAreMixedAccountsSignedIn") == 0);
    CHECK(caller.XAM("XamUserGetUserFlags", {0}) == 0);
    CHECK(caller.XAM("XamUserGetUserFlags", {255}) == 0);
    CHECK(caller.XAM("XamIsCurrentTitleDash") == 0);
    CHECK(caller.XAM("XamGetUserNeedTFA", {0, output, output + 4}) == 0x80070525);
    CHECK(caller.Load32(output) == 0xCDCDCDCD);
    auto& profile = rex::xam::CurrentXAM().GetUserProfile();
    REQUIRE(profile.CreateLocal("Local User") == 0);
    REQUIRE(profile.SetLocalSession(profile.GetXUID()) == 0);
    CHECK(caller.XAM("XamGetUserNeedTFA", {0, 0, output}) == 0x80004003);
    CHECK(caller.XAM("XamGetUserNeedTFA", {0, output, output + 4}) == 0);
    CHECK(caller.Load32(output) == 0);
    CHECK(caller.Load32(output + 4) == 0);
    const auto before = rex::Clock::QueryGuestSystemTime();
    caller.XAM("GetSystemTimeAsFileTime", {output + 8});
    const auto value = (uint64_t(caller.Load32(output + 8)) << 32) | caller.Load32(output + 12);
    CHECK(value >= before);
    CHECK(value <= rex::Clock::QueryGuestSystemTime());
    caller.XAM("GetSystemTimeAsFileTime", {0});
  });
}
