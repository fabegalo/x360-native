#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/task_context.h>
#include <stdexcept>
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
extern "C" REX_FUNC(__imp__XamTaskGetAttributes);

namespace {
std::atomic<uint32_t> received_context, received_handle, worker_handle;
std::atomic<uint32_t> worker_system_task;
std::atomic<bool> entered, cancelled, finished;
void TaskCallback(PPCContext& ctx, uint8_t* base) {
  worker_system_task = rex::kernel::xam::CurrentSystemTask();
  received_context = ctx.r3.u32;
  worker_handle = rex::system::XThread::GetCurrentThread()->handle();
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
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  const auto output = rt.memory()->SystemHeapAlloc(8);
  auto* base = rt.memory()->virtual_membase();
  uint32_t outside = ~0u, outer = 0, inner = 0, restored = 0, after = ~0u;
  uint32_t attributes = 0, attr_result = ~0u, invalid = 0, result = 0, null_output = 0;
  bool host_identity = false;
  bool outer_alive = false, inner_gone = false, exception_restored = false;
  auto caller = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&] {
    auto& ctx = *rex::system::XThread::GetCurrentThread()->thread_state()->context();
    auto current = [&] {
      __imp__XamTaskGetCurrentTask(ctx, base);
      const auto handle = ctx.r3.u32;
      if (handle) __imp__XamTaskCloseHandle(ctx, base);
      return handle;
    };
    outside = current();
    result = rex::kernel::xam::RunSystemTask([&] {
      outer = current();
      ctx.r3.u64 = rex::kernel::xam::CurrentSystemTask();
      host_identity = ctx.r3.u32 == outer;
      __imp__XamTaskCloseHandle(ctx, base);
      outer_alive = rex::kernel::xam::IsRuntimeTask(outer);
      ctx.r3.u64 = outer; ctx.r4.u64 = output;
      __imp__XamTaskGetAttributes(ctx, base); attr_result = ctx.r3.u32;
      attributes = rex::memory::load_and_swap<uint32_t>(base + output);
      ctx.r3.u64 = outer; ctx.r4.u64 = 0;
      __imp__XamTaskGetAttributes(ctx, base); null_output = ctx.r3.u32;
      rex::kernel::xam::RunSystemTask([&] { inner = current(); return 0u; });
      inner_gone = !rex::kernel::xam::IsRuntimeTask(inner);
      restored = current();
      try {
        rex::kernel::xam::RunSystemTask([]() -> uint32_t { throw std::runtime_error("test"); });
      } catch (const std::runtime_error&) { exception_restored = current() == outer; }
      return 0x81234567u;
    });
    after = current();
    ctx.r3.u64 = outer; ctx.r4.u64 = output;
    __imp__XamTaskGetAttributes(ctx, base); invalid = ctx.r3.u32;
    return 0;
  }));
  REQUIRE(caller->Create() == X_STATUS_SUCCESS);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(caller->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
  CHECK(outside == 0); CHECK(after == 0);
  CHECK(outer != 0); CHECK(inner != 0); CHECK(inner != outer);
  CHECK(outer_alive); CHECK(inner_gone); CHECK(restored == outer);
  CHECK(exception_restored); CHECK(attr_result == 0);
  CHECK(attributes == 0x04000000);
  CHECK(host_identity); CHECK(null_output == 0x80070057);
  CHECK(invalid == 0x80070057); CHECK(result == 0x81234567);
  CHECK_FALSE(rex::kernel::xam::IsRuntimeTask(outer));
  rt.memory()->SystemHeapFree(output);
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
  CHECK(worker_system_task == 0);
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
