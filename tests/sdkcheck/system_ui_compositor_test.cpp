#include <catch2/catch_test_macros.hpp>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/xthread.h>
#include "../../titles/common/system_ui_context.h"
#include "../../titles/dash_17559/src/system_ui_compositor.h"
#include <atomic>
#include <stdexcept>

TEST_CASE("17559 notification compositor borrows each worker and restores it on unwind",
          "[sdk][notification-compositor]") {
  rex::Runtime runtime(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == 0);
  auto* memory = runtime.memory();
  REQUIRE(memory->LookupHeap(0x81AA0000)->AllocFixed(0x81AA0000, 0x10000, 0x10000,
      rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite));
  auto* worker = memory->TranslateVirtual<rex::be<uint32_t>*>(0x81AA09F0);
  *worker = 0x12340000;
  std::atomic<unsigned> passed{0};
  for (unsigned i = 0; i < 2; ++i) {
    auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        runtime.kernel_state(), 128 * 1024, 0, [&] {
      x360n::SystemUiContext context(*memory);
      auto* thread = rex::system::XThread::GetCurrentThread();
      const auto current = thread->guest_object();
      bool bound = false, nested = false;
      try {
        dash17559::SystemCompositorContext outer(*memory, context.thread_state());
        bound = *worker == current;
        {
          dash17559::SystemCompositorContext inner(*memory, context.thread_state());
        }
        nested = *worker == current;
        // Destruction uses the stored word, not a possibly unwound PPC r13.
        context.thread_state()->context()->r13.u64 = 0;
        throw std::runtime_error("guest composition unwind");
      } catch (const std::runtime_error&) {}
      if (bound && nested && *worker == 0x12340000) ++passed;
      return 0;
    }));
    REQUIRE(caller->Create() == 0);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(caller->Wait(0, 0, 0, &timeout) == 0);
    CHECK(passed.load() == i + 1);
    CHECK(*worker == 0x12340000);
  }
}
