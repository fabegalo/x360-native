#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>

extern "C" REX_FUNC(__imp__XamVoiceSetMicArrayIdleUsers);
using rex::X_STATUS;

TEST_CASE("Mic array idle-user control does not return the incoming user mask",
          "[sdk][mic-array-idle]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  CHECK(rt.kernel_state()->GetMicArrayIdleUsers() == 0);
  CHECK_FALSE(rt.kernel_state()->TakeMicArrayIdleUsersUpdate().has_value());
  uint32_t results[3]{};
  const uint32_t masks[] = {1, 15, 0xFFFF0000};
  auto thread = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&] {
    auto& ctx = *rex::system::XThread::GetCurrentThread()->thread_state()->context();
    for (unsigned i = 0; i < 3; ++i) {
      ctx.r3.u64 = masks[i];
      ctx.r4.u64 = i;
      __imp__XamVoiceSetMicArrayIdleUsers(ctx, rt.memory()->virtual_membase());
      results[i] = ctx.r3.u32;
    }
    return 0;
  }));
  REQUIRE(thread->Create() == X_STATUS_SUCCESS);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
  for (auto result : results) CHECK(result == 0);
  CHECK(rt.kernel_state()->GetMicArrayIdleUsers() == 0xFFFF0000);
  CHECK(rt.kernel_state()->TakeMicArrayIdleUsersUpdate() == 0xFFFF0000);
  CHECK_FALSE(rt.kernel_state()->TakeMicArrayIdleUsersUpdate().has_value());
  // Notifications are optional and coalesced, while every setter updates state.
  rt.kernel_state()->SetMicArrayIdleUsers(3, false);
  CHECK(rt.kernel_state()->GetMicArrayIdleUsers() == 3);
  CHECK_FALSE(rt.kernel_state()->TakeMicArrayIdleUsersUpdate().has_value());
  rt.kernel_state()->SetMicArrayIdleUsers(7, true);
  rt.kernel_state()->SetMicArrayIdleUsers(0, false);
  CHECK(rt.kernel_state()->TakeMicArrayIdleUsersUpdate() == 0);
  CHECK_FALSE(rt.kernel_state()->TakeMicArrayIdleUsersUpdate().has_value());
}
