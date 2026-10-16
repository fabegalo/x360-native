#include <catch2/catch_test_macros.hpp>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include "../../titles/common/system_ui_context.h"
#include <atomic>
#include <stdexcept>

TEST_CASE("System UI borrows each presenting thread and restores its context on unwind",
          "[sdk][guide-context]") {
  rex::Runtime runtime(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == 0);
  std::atomic<unsigned> passed{0};
  // The same saved callback is invoked by two independent guest threads.
  // It must bind to the invoking thread, even after the first one exits.
  auto callback = [&] {
    auto* state = rex::system::XThread::GetCurrentThread()->thread_state();
    auto* ctx = state->context();
    auto* thread = rex::system::XThread::GetCurrentThread()
                       ->guest_object<rex::system::X_KTHREAD>();
    auto* pcr = runtime.memory()->TranslateVirtual<rex::system::X_KPCR*>(ctx->r13.u32);
    const auto process = thread->process_type;
    const auto duplicate = thread->process_type_dup;
    const auto dpc = pcr->processtype_value_in_dpc;
    ctx->r3.u64 = 0x123456789ABCDEF0;
    ctx->lr = 0xDEADBEEF;
    ctx->f1.f64 = 1.25;
    const PPCContext saved = *ctx;
    bool borrowed = false, nested = false;
    try {
      x360n::SystemUiContext outer(*runtime.memory());
      borrowed = outer.thread_state() == state && thread->process_type == 2 &&
                 thread->process_type_dup == 2 && pcr->processtype_value_in_dpc == 2;
      ctx->r3.u64 = 42;
      {
        x360n::SystemUiContext inner(*runtime.memory());
        ctx->r3.u64 = 77;
      }
      nested = ctx->r3.u64 == 42 && thread->process_type == 2;
      ctx->r13.u64 = 0;
      ctx->lr = 0;
      ctx->f1.f64 = -1;
      ctx->fpscr.enableFlushMode();
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
    auto thread = rex::system::object_ref<rex::system::XHostThread>(
        new rex::system::XHostThread(runtime.kernel_state(), 128 * 1024, 0, callback));
    REQUIRE(thread->Create() == 0);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == 0);
    CHECK(passed.load() == i + 1);
  }
}
