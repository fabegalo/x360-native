#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"

TEST_CASE("Initial storage availability reaches the system notification listener",
          "[sdk][storage-notification]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([](rex::test::Caller& c) {
    const auto id = c.Alloc(4), param = c.Alloc(4);
    const auto live = c.XAM("XamNotifyCreateListener", {2, 10});
    REQUIRE(live != 0);
    CHECK(c.XAM("XNotifyGetNext", {live, 0, id, param}) == 0);
    const auto system = c.XAM("XamNotifyCreateListener", {0x23, 10});
    REQUIRE(system != 0);
    unsigned storage_count = 0;
    unsigned dequeued = 0;
    while (c.XAM("XNotifyGetNext", {system, 0, id, param})) {
      REQUIRE(++dequeued <= 32);
      if (c.Load32(id) == 0xB) {
        ++storage_count;
        CHECK(c.Load32(param) == 0);
      }
    }
    CHECK(storage_count == 1);
    CHECK(c.XAM("XNotifyGetNext", {system, 0, id, param}) == 0);
    CHECK(c.XAM("XNotifyGetNext", {live, 0, id, param}) == 0);
    CHECK(c.Kernel("NtClose", {system}) == 0);
    CHECK(c.Kernel("NtClose", {live}) == 0);
  });
}
