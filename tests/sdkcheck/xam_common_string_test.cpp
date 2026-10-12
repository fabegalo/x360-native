#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

extern "C" REX_FUNC(__imp__XamLookupCommonStringByIndex);
extern "C" REX_FUNC(__imp__XamLaunchURI);

TEST_CASE("Missing XAM common strings return null instead of an ID as a pointer", "[sdk][common-string]") {
  auto ctx = std::make_unique<PPCContext>();
  // Actual IDs used by the dashboard's confirmation dialog; null takes its
  // existing failure path (-1), not the success/confirmation path (0).
  for (uint32_t id : {8u, 11u, 272u, 275u, 0xFFFFFFFFu}) {
    CAPTURE(id);
    ctx->r3.u64 = id;
    ctx->r4.u64 = 0xBADC0FFEE;
    __imp__XamLookupCommonStringByIndex(*ctx, nullptr);
    CHECK(ctx->r3.u64 == 0);
    CHECK(ctx->r4.u64 == 0xBADC0FFEE);
  }
}

TEST_CASE("Dashboard unsupported URI launch never reports success or completion", "[sdk][uri]") {
  auto ctx = std::make_unique<PPCContext>();
  for (uint32_t user : {0u, 1u, 3u, 0xFFFFFFFFu}) {
    ctx->r3.u64 = user;
    ctx->r4.u64 = 0x40665620;  // Captured URI pointer; must not be dereferenced.
    ctx->r5.u64 = 5;
    ctx->r6.u64 = 0x7015EDF0;  // Must not signal or write to OVERLAPPED.
    __imp__XamLaunchURI(*ctx, nullptr);
    CHECK(ctx->r3.u32 == 0x80004001);
    CHECK(ctx->r4.u64 == 0x40665620);
    CHECK(ctx->r5.u64 == 5);
    CHECK(ctx->r6.u64 == 0x7015EDF0);
  }
}
