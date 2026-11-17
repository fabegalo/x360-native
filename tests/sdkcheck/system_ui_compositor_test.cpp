#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/object/object_table.h>
#include "../../titles/common/system_ui_context.h"
#include "../../titles/dash_17559/src/system_ui_compositor.h"
#include <atomic>
#include <stdexcept>

TEST_CASE("17559 notification compositor borrows each worker and restores it on unwind",
          "[sdk][notification-compositor]") {
  rex::test::KernelFixture fixture;
  auto& runtime = fixture.GetRuntime();
  auto* memory = &rex::kernel::GetAddressSpace(runtime);
  REQUIRE(memory->LookupHeap(0x81AA0000)->AllocFixed(0x81AA0000, 0x10000, 0x10000,
      rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
      rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite));
  auto* worker = memory->TranslateVirtual<rex::be_u32*>(0x81AA09F0);
  *worker = 0x12340000;
  std::atomic<unsigned> passed{0};
  for (unsigned i = 0; i < 2; ++i) {
    fixture.RunOnGuestThread([&](rex::test::Caller&) {
      x360n::SystemUiContext context(*memory);
      auto* thread = rex::kernel::KernelThread::GetCallingThread();
      const auto current = thread->GetGuestObject();
      bool bound = false, nested = false;
      try {
        dash17559::SystemCompositorContext outer(*memory, context.thread_state());
        bound = *worker == current;
        {
          dash17559::SystemCompositorContext inner(*memory, context.thread_state());
        }
        nested = *worker == current;
        // Destruction uses the stored word, not a possibly unwound PPC r13.
        context.thread_state()->GetContext()->r13.u64 = 0;
        throw std::runtime_error("guest composition unwind");
      } catch (const std::runtime_error&) {}
      if (bound && nested && *worker == 0x12340000) ++passed;
    });
    CHECK(passed.load() == i + 1);
    CHECK(*worker == 0x12340000);
  }
}
