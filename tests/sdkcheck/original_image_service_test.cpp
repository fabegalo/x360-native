#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <array>
#include "synthetic_xex.h"

extern "C" REX_FUNC(__imp__XamReadImage);
extern "C" REX_FUNC(__imp__XamInitializeGameTileCache);
extern "C" REX_FUNC(__imp__XamDestroyGameTileCache);
extern "C" REX_FUNC(__imp__XamReadGameTileImage);

namespace {
unsigned calls;
void ImageProbe(PPCContext& ctx, uint8_t*) {
  ++calls;
  CHECK(ctx.r3.u32 == 10); CHECK(ctx.r4.u32 == 0);
  CHECK(ctx.r5.u32 == 0xFE); CHECK(ctx.r6.u64 == 0xE000001122334455ull);
  CHECK(ctx.r7.u32 == 0x12345678); CHECK(ctx.r8.u32 == 0x87654321);
  ctx.r3.u64 = 1;
}
}

TEST_CASE("Retail 17559 image services preserve original cache ABI and full tile key",
          "[sdk][original-image-service]") {
  x360n::test::Workdir dir("x360n-original-images", "routing");
  rex::Runtime rt(dir.dir, dir.dir); rex::RuntimeConfig config{};
  config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto* dispatcher = rt.function_dispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x81690000,0x10000,0x81690000,0x10000,true,true));
  const std::array<PPCFunc*,4> functions{__imp__XamReadImage,
      __imp__XamInitializeGameTileCache,__imp__XamDestroyGameTileCache,__imp__XamReadGameTileImage};
  const std::array<uint32_t,4> addresses{0x81691EE8,0x81691DD0,0x81691E90,0x81691EC8};
  for (size_t i=0;i<functions.size();++i) {
    PPCContext ctx{}; ctx.r3.u64=10;ctx.r4.u64=0;ctx.r5.u64=0xFE;
    ctx.r6.u64=0xE000001122334455ull;ctx.r7.u64=0x12345678;ctx.r8.u64=0x87654321;
    calls=0; functions[i](ctx,rt.memory()->virtual_membase());
    CHECK(ctx.r3.u32==0x80004001); CHECK(calls==0);
    REQUIRE(dispatcher->SetFunction(addresses[i],ImageProbe));
    ctx.r3.u64=10;functions[i](ctx,rt.memory()->virtual_membase());
    CHECK(ctx.r3.u32==1);CHECK(calls==1);
  }
}
