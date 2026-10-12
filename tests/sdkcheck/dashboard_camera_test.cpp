#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/xtypes.h>

extern "C" REX_FUNC(__imp__XUsbcamCreate);
extern "C" REX_FUNC(__imp__XUsbcamGetState);

TEST_CASE("dashboard camera creation reports unsupported without claiming a device", "[sdk][dashboard-camera]") {
  using rex::X_STATUS;
  auto ctx = std::make_unique<PPCContext>();
  ctx->r3.u64 = 0x10000;
  ctx->r4.u64 = 0x4B000;
  ctx->r5.u64 = 0x20000;
  // A failure needs no mapped buffer and must not write a synthetic handle.
  __imp__XUsbcamCreate(*ctx, nullptr);
  CHECK(ctx->r3.u32 == X_STATUS_NOT_IMPLEMENTED);
  CHECK(ctx->r4.u32 == 0x4B000);
  CHECK(ctx->r5.u32 == 0x20000);
  __imp__XUsbcamGetState(*ctx, nullptr);
  CHECK(ctx->r3.u32 == 0);
}
