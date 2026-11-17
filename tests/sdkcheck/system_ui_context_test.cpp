#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include "../../titles/common/system_ui_context.h"
#include "../../titles/common/system_ui_execution.h"
#include <atomic>
#include <future>
#include <stdexcept>

TEST_CASE("System UI borrows each presenting thread and restores its context on unwind",
          "[sdk][guide-context]") {
  rex::test::KernelFixture fixture;
  auto& runtime = fixture.GetRuntime();
  std::atomic<unsigned> passed{0};
  // The same saved callback is invoked by two independent guest threads.
  // It must bind to the invoking thread, even after the first one exits.
  auto callback = [&] {
    auto* state = rex::kernel::KernelThread::GetCallingThread()->GetThreadState();
    auto* ctx = state->GetContext();
    auto* thread = rex::kernel::KernelThread::GetCallingThread()
                       ->GetGuestObject<rex::kernel::X_KTHREAD>();
    auto* pcr = rex::kernel::KernelThread::GetCallingThread()->GetAddressSpace()->TranslateVirtual<rex::kernel::X_KPCR*>(ctx->r13.u32);
    const auto process = thread->process_type;
    const auto duplicate = thread->process_type_dup;
    const auto dpc = pcr->processtype_value_in_dpc;
    ctx->r3.u64 = 0x123456789ABCDEF0;
    ctx->lr = 0xDEADBEEF;
    ctx->f1.f64 = 1.25;
    const PPCContext saved = *ctx;
    bool borrowed = false, nested = false;
    try {
      x360n::SystemUiContext outer(*rex::kernel::KernelThread::GetCallingThread()->GetAddressSpace());
      borrowed = outer.thread_state() == state && thread->process_type == 2 &&
                 thread->process_type_dup == 2 && pcr->processtype_value_in_dpc == 2;
      ctx->r3.u64 = 42;
      {
        x360n::SystemUiContext inner(*rex::kernel::KernelThread::GetCallingThread()->GetAddressSpace());
        ctx->r3.u64 = 77;
      }
      nested = ctx->r3.u64 == 42 && thread->process_type == 2;
      ctx->r13.u64 = 0;
      ctx->lr = 0;
      ctx->f1.f64 = -1;
      ctx->fpscr.EnableFlushMode();
      throw std::runtime_error("synthetic guest unwind");
    } catch (const std::runtime_error&) {}
    if (borrowed && nested && ctx->r3.u64 == saved.r3.u64 &&
        ctx->r13.u64 == saved.r13.u64 && ctx->lr == saved.lr &&
        ctx->f1.f64 == saved.f1.f64 && ctx->fpscr.csr == saved.fpscr.csr &&
        thread->process_type == process && thread->process_type_dup == duplicate &&
        pcr->processtype_value_in_dpc == dpc) ++passed;
    return 0;
  };
  for (unsigned i = 0; i < 2; ++i) {
    auto thread = runtime.GetThreadRegistry().LaunchHostThread("guide-context", 128 * 1024, callback);
    REQUIRE(thread);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(runtime.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*thread, 0, 0, 0, &timeout) == 0);
    REQUIRE(runtime.GetObjectTable().Close(*thread));
    CHECK(passed.load() == i + 1);
  }
}

TEST_CASE("System UI teardown joins the Xbox Home caller after foreground exit",
          "[sdk][guide-context][guide-exit]") {
  rex::test::KernelFixture fixture;
  auto& runtime = fixture.GetRuntime();
  x360n::SystemUiExecution execution;
  std::promise<bool> entered;
  std::promise<void> allow_exit;
  auto release = allow_exit.get_future();
  auto caller = runtime.GetThreadRegistry().LaunchHostThread("guide-exit", 128 * 1024, [&] {
        // Keep the claim as if a guest exit bypassed the host frame's destructor.
        entered.set_value(execution.Begin());
        release.wait();
        // The foreground thread's earlier exit is not sufficient.
        rex::kernel::KernelThread::GetCallingThread()->Exit(0);
        return 0;
      });
  REQUIRE(caller);
  CHECK(entered.get_future().get());
  std::atomic<bool> overlapping_frame{true};
  auto other = runtime.GetThreadRegistry().LaunchHostThread("guide-overlap", 128 * 1024, [&] {
        x360n::SystemUiExecution::Scope frame(execution);
        overlapping_frame = bool(frame);
        return 0;
      });
  REQUIRE(other);
  uint64_t timeout = uint64_t(-50'000'000ll);
  CHECK(runtime.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*other, 0, 0, 0, &timeout) == 0);
  REQUIRE(runtime.GetObjectTable().Close(*other));
  CHECK_FALSE(overlapping_frame.load());
  std::promise<void> waiting;
  auto teardown = std::async(std::launch::async, [&] {
    waiting.set_value();
    execution.WaitForCallerExit();
  });
  waiting.get_future().wait();
  CHECK(teardown.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
  allow_exit.set_value();
  CHECK(teardown.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  teardown.get();
  REQUIRE(runtime.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller, 0, 0, 0, &timeout) == 0);
  REQUIRE(runtime.GetObjectTable().Close(*caller));
}
