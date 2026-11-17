#include <catch2/catch_test_macros.hpp>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/types/thread.h>
#include <rex/memory/mapped_ptr.h>
#include "kernel/kernel_fixture.h"
#include "kernel/title_state.h"

namespace {
struct Observation {
  uint32_t calls = 0, address = 0, context = 0, arg1 = 0, arg2 = 0;
  bool dispatch_level = true, inactive_after = false, registers_preserved = false;
  uint32_t inserted = 0, removed = 0;
} observed;

void Callback(PPCContext& ctx, uint8_t* base) {
  auto* pcr = rex::memory::GuestPtr<rex::kernel::X_KPCR*>(base, ctx.r13.u32);
  ++observed.calls;
  observed.address = ctx.r3.u32;
  observed.context = ctx.r4.u32;
  observed.arg1 = ctx.r5.u32;
  observed.arg2 = ctx.r6.u32;
  observed.dispatch_level &= pcr->current_irql == rex::kernel::kIRQLDispatch &&
                             pcr->prcb_data.dpc_active == 1;
  // A DPC must not corrupt the interrupted caller's volatile state.
  ctx.r7.u64 = 0xDEAD;
  ctx.cr6.lt = true;
}
}

TEST_CASE("DPC insertion executes the real callback and leaves it reusable", "[sdk][dpc]") {
  observed = {};
  rex::test::KernelFixture fixture;
  auto* dispatcher = rex::kernel::StateOf(fixture.GetRuntime()).GetFunctionDispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
  REQUIRE(dispatcher->SetFunction(0x82010000, Callback));
  uint32_t address = 0;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    address = caller.Alloc(sizeof(rex::kernel::X_KDPC));
    auto* dpc = rex::memory::GuestPtr<rex::kernel::X_KDPC*>(caller.GetBase(), address);
    dpc->Initialize(0x82010000, 0x11223344);
    auto* ctx = &caller.GetContext();
    auto* pcr = rex::memory::GuestPtr<rex::kernel::X_KPCR*>(caller.GetBase(), ctx->r13.u32);
    pcr->current_irql = rex::kernel::kIRQLAPC;
    for (int i = 0; i < 2; ++i) {
      ctx->r3.u64 = address;
      ctx->r4.u64 = 0x55667788;
      ctx->r5.u64 = 0x99AABBCC;
      ctx->r7.u64 = 0x12345678;
      ctx->cr6.lt = false;
      caller.Kernel("KeInsertQueueDpc", {address, 0x55667788, 0x99AABBCC});
      observed.inserted += ctx->r3.u32;
      observed.registers_preserved = ctx->r7.u64 == 0x12345678 && !ctx->cr6.lt;
    }
    observed.inactive_after = pcr->current_irql == rex::kernel::kIRQLAPC &&
                              pcr->prcb_data.dpc_active == 0;
    ctx->r3.u64 = address;
    caller.Kernel("KeRemoveQueueDpc", {address});
    observed.removed = ctx->r3.u32;
  });
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
