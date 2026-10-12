#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>

extern "C" REX_FUNC(__imp__KeInsertQueueDpc);
extern "C" REX_FUNC(__imp__KeRemoveQueueDpc);
using rex::X_STATUS;

namespace {
struct Observation {
  uint32_t calls = 0, address = 0, context = 0, arg1 = 0, arg2 = 0;
  bool dispatch_level = true, inactive_after = false, registers_preserved = false;
  uint32_t inserted = 0, removed = 0;
} observed;

void Callback(PPCContext& ctx, uint8_t*) {
  auto* memory = rex::Runtime::instance()->memory();
  auto* pcr = memory->TranslateVirtual<rex::system::X_KPCR*>(ctx.r13.u32);
  ++observed.calls;
  observed.address = ctx.r3.u32;
  observed.context = ctx.r4.u32;
  observed.arg1 = ctx.r5.u32;
  observed.arg2 = ctx.r6.u32;
  observed.dispatch_level &= pcr->current_irql == rex::system::IRQL_DISPATCH &&
                             pcr->prcb_data.dpc_active == 1;
  // A DPC must not corrupt the interrupted caller's volatile state.
  ctx.r7.u64 = 0xDEAD;
  ctx.cr6.lt = true;
}
}

TEST_CASE("DPC insertion executes the real callback and leaves it reusable", "[sdk][dpc]") {
  observed = {};
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* dispatcher = rt.function_dispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
  REQUIRE(dispatcher->SetFunction(0x82010000, Callback));
  const uint32_t address = rt.memory()->SystemHeapAlloc(sizeof(rex::system::XDPC));
  REQUIRE(address != 0);
  auto* dpc = rt.memory()->TranslateVirtual<rex::system::XDPC*>(address);
  dpc->Initialize(0x82010000, 0x11223344);
  dpc->list_entry = {};
  auto thread = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&]() {
    auto* current = rex::system::XThread::GetCurrentThread();
    auto* ctx = current->thread_state()->context();
    auto* pcr = rt.memory()->TranslateVirtual<rex::system::X_KPCR*>(ctx->r13.u32);
    pcr->current_irql = rex::system::IRQL_APC;
    for (int i = 0; i < 2; ++i) {
      ctx->r3.u64 = address;
      ctx->r4.u64 = 0x55667788;
      ctx->r5.u64 = 0x99AABBCC;
      ctx->r7.u64 = 0x12345678;
      ctx->cr6.lt = false;
      __imp__KeInsertQueueDpc(*ctx, rt.memory()->virtual_membase());
      observed.inserted += ctx->r3.u32;
      observed.registers_preserved = ctx->r7.u64 == 0x12345678 && !ctx->cr6.lt;
    }
    observed.inactive_after = pcr->current_irql == rex::system::IRQL_APC &&
                              pcr->prcb_data.dpc_active == 0;
    ctx->r3.u64 = address;
    __imp__KeRemoveQueueDpc(*ctx, rt.memory()->virtual_membase());
    observed.removed = ctx->r3.u32;
    return 0;
  }));
  REQUIRE(thread->Create() == X_STATUS_SUCCESS);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
  CHECK(observed.calls == 2);
  CHECK(observed.inserted == 2);
  CHECK(observed.address == address);
  CHECK(observed.context == 0x11223344);
  CHECK(observed.arg1 == 0x55667788);
  CHECK(observed.arg2 == 0x99AABBCC);
  CHECK(observed.dispatch_level);
  CHECK(observed.inactive_after);
  CHECK(observed.registers_preserved);
  CHECK(observed.removed == 0);
}
