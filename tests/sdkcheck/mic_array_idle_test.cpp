#include "kernel/kernel_fixture.h"
#include "xam/xam_state.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Mic array idle-user control does not return the incoming user mask",
          "[sdk][mic-array-idle]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    auto &state = rex::xam::CurrentXAM();
    CHECK(state.GetMicArrayIdleUsers() == 0);
    CHECK_FALSE(state.TakeMicArrayIdleUsersUpdate().has_value());
    uint32_t results[3]{};
    const uint32_t masks[] = {1, 15, 0xFFFF0000};
    for (unsigned i = 0; i < 3; ++i) {
      results[i] = caller.XAM("XamVoiceSetMicArrayIdleUsers", {masks[i], i});
    }
    for (auto result : results)
      CHECK(result == 0);
    CHECK(state.GetMicArrayIdleUsers() == 0xFFFF0000);
    CHECK(state.TakeMicArrayIdleUsersUpdate() == 0xFFFF0000);
    CHECK_FALSE(state.TakeMicArrayIdleUsersUpdate().has_value());
    // Notifications are optional and coalesced, while every setter updates
    // state.
    state.SetMicArrayIdleUsers(3, false);
    CHECK(state.GetMicArrayIdleUsers() == 3);
    CHECK_FALSE(state.TakeMicArrayIdleUsersUpdate().has_value());
    state.SetMicArrayIdleUsers(7, true);
    state.SetMicArrayIdleUsers(0, false);
    CHECK(state.TakeMicArrayIdleUsersUpdate() == 0);
    CHECK_FALSE(state.TakeMicArrayIdleUsersUpdate().has_value());
  });
}
