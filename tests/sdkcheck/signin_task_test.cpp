#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>

using rex::X_STATUS;
extern "C" REX_FUNC(__imp__XamTaskSchedule);
extern "C" REX_FUNC(__imp__XamTaskShouldExit);
extern "C" REX_FUNC(__imp__XamTaskCancel);
extern "C" REX_FUNC(__imp__XamTaskWaitOnCompletion);
extern "C" REX_FUNC(__imp__XamTaskCloseHandle);
extern "C" REX_FUNC(__imp__XamTaskGetCurrentTask);
extern "C" REX_FUNC(__imp__XamTaskGetStatus);
extern "C" REX_FUNC(__imp__XamTaskGetCompletionStatus);

namespace {
std::atomic<uint32_t> received_context, received_handle, worker_handle;
std::atomic<bool> entered, cancelled, finished;
void TaskCallback(PPCContext& ctx, uint8_t* base) {
  received_context = ctx.r3.u32;
  worker_handle = rex::system::XThread::GetCurrentThread()->handle();
  __imp__XamTaskGetCurrentTask(ctx, base);
  received_handle = ctx.r3.u32;
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
}

TEST_CASE("Sign-in task cancellation joins callback before releasing its module",
          "[sdk][signin-task]") {
  received_context = received_handle = 0;
  entered = cancelled = finished = false;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* dispatcher = rt.function_dispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
  REQUIRE(dispatcher->SetFunction(0x82010000, TaskCallback));
  auto address = rt.memory()->SystemHeapAlloc(16);
  auto* base = rt.memory()->virtual_membase();
  uint32_t scheduled = ~0u, pending = 0, joined = ~0u, result = 0, closed = ~0u, stale = 0, complete = 0;
  bool completed_at_join = false;
  auto caller = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&] {
    auto& ctx = *rex::system::XThread::GetCurrentThread()->thread_state()->context();
    ctx.r3.u64 = 0x82010000; ctx.r4.u64 = 0x10203040;
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
    return 0;
  }));
  REQUIRE(caller->Create() == X_STATUS_SUCCESS);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(caller->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
  CHECK(scheduled == 0);
  CHECK(received_context == 0x10203040);
  CHECK(received_handle == rex::memory::load_and_swap<uint32_t>(base + address));
  CHECK(pending == 0);
  CHECK(cancelled);
  CHECK(joined == 0);
  CHECK(completed_at_join);
  CHECK(complete == 1);
  CHECK(result == 0x81234567);
  CHECK(closed == 0);
  CHECK(stale == 0x80070057u);
  CHECK_FALSE(rt.kernel_state()->object_table()->LookupObject<rex::system::XThread>(worker_handle));
  rt.memory()->SystemHeapFree(address);
}
