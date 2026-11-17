// KeGetImagePageTableEntry for pages of the 4 KB-page XEX heap (patch 0005).
//
// dash.xex 2.0.17559.0 validates the buffers of its Lua chunks (0x928EF408): the
// address must be in 0x80000000-0x9FFFFFFF and bit 0 of KeGetImagePageTableEntry
// for its page must be set, else KeBugCheck(0xF7). At f5337cdc the export returned
// the page index | 0x40000000, so bit 0 was clear for every even page: the dashboard
// stopped on 0x92FD2204 (run 20261012T011201Z-dash17559-diag-luabuf).
// patches/rexglue-sdk/0005 sets bit 0 for that heap, as Xenia Canary 331ddf79bf96.
#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"

TEST_CASE("KeGetImagePageTableEntry sets bit 0 for pages of the 4 KB-page XEX heap",
          "[sdk][kernel]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([](rex::test::Caller& c) {
    CHECK(c.Kernel("KeGetImagePageTableEntry", {0x92FD2204}) == 0x40002FD3);
    CHECK(c.Kernel("KeGetImagePageTableEntry", {0x92135820}) == 0x40002135);
    CHECK(c.Kernel("KeGetImagePageTableEntry", {0x90000000}) == 0x40000001);
    CHECK(c.Kernel("KeGetImagePageTableEntry", {0x82020000}) == 0x202);
    CHECK(c.Kernel("KeGetImagePageTableEntry", {0x82010000}) == 0x201);
    CHECK(c.Kernel("KeGetImagePageTableEntry", {0x40000000}) == 0);
  });
}
