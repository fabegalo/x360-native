#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include "../../titles/dash_17559/src/system_ui_task.h"

TEST_CASE("17559 original task predicate observes host cancellation without retaining handles",
          "[sdk][original-task-predicate]") {
  rex::test::KernelFixture fixture;
  uint32_t completed_task = 0;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    CHECK_FALSE(dash17559::RuntimeSystemTaskShouldExit()); // Fall back to original TLS.
    CHECK(rex::xam::RunSystemTask([&] {
      completed_task = rex::xam::CurrentSystemTask();
      REQUIRE(completed_task);
      for (unsigned i = 0; i < 64; ++i)
        CHECK(dash17559::RuntimeSystemTaskShouldExit() == 0);
      CHECK(caller.XAM("XamTaskCancel", {completed_task}) == 0);
      for (unsigned i = 0; i < 64; ++i)
        CHECK(dash17559::RuntimeSystemTaskShouldExit() == 1);
      CHECK(caller.XAM("XamTaskCloseHandle", {completed_task}) == 0);
      return 0u;
    }) == 0);
    CHECK_FALSE(dash17559::RuntimeSystemTaskShouldExit());
    CHECK_FALSE(rex::xam::IsRuntimeTask(completed_task));
  });
}
