#include <catch2/catch_test_macros.hpp>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/xthread.h>
#include "../../titles/dash_17559/src/system_ui_task.h"

namespace rex::kernel::xam { uint32_t XamTaskCancel_entry(uint32_t handle); }

TEST_CASE("17559 original task predicate observes host cancellation without retaining handles",
          "[sdk][original-task-predicate]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  uint32_t completed_task = 0;
  auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(), 128 * 1024, 0, [&] {
    CHECK_FALSE(dash17559::RuntimeSystemTaskShouldExit()); // Fall back to original TLS.
    CHECK(rex::kernel::xam::RunSystemTask([&] {
      completed_task = rex::kernel::xam::CurrentSystemTask();
      REQUIRE(completed_task);
      for (unsigned i = 0; i < 64; ++i)
        CHECK(dash17559::RuntimeSystemTaskShouldExit() == 0);
      CHECK(rex::kernel::xam::XamTaskCancel_entry(completed_task) == 0);
      for (unsigned i = 0; i < 64; ++i)
        CHECK(dash17559::RuntimeSystemTaskShouldExit() == 1);
      CHECK(rex::kernel::xam::XamTaskCloseHandle_entry(completed_task) == 0);
      return 0u;
    }) == 0);
    CHECK_FALSE(dash17559::RuntimeSystemTaskShouldExit());
    CHECK_FALSE(rex::kernel::xam::IsRuntimeTask(completed_task));
    return 0;
  }));
  REQUIRE(caller->Create() == 0);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(caller->Wait(0, 0, 0, &timeout) == 0);
}
