#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include "../../titles/dash_17559/src/display_transition.h"

extern "C" REX_FUNC(__imp__VdPersistDisplay);

TEST_CASE("VdPersistDisplay retains the actual descriptor and transfers its allocation", "[sdk][persist-display]") {
  using rex::X_STATUS;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* memory = rt.memory();
  const auto scratch = memory->SystemHeapAlloc(64);
  const auto image = memory->SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  REQUIRE(scratch);
  REQUIRE(image);
  auto* bytes = memory->TranslateVirtual(scratch);
  PPCContext ctx{};
  auto invoke = [&] {
    ctx.r3.u64 = scratch;
    ctx.r4.u64 = scratch + 32;
    __imp__VdPersistDisplay(ctx, memory->virtual_membase());
    return ctx.r3.u32;
  };
  std::memset(bytes, 0, 64);
  CHECK(invoke() == 0); // no dummy image to free on the first present
  CHECK(rex::memory::load_and_swap<uint32_t>(bytes + 32) == 0);
  rex::memory::store_and_swap<uint32_t>(bytes, image);
  rex::memory::store_and_swap<uint16_t>(bytes + 4, 16);
  rex::memory::store_and_swap<uint16_t>(bytes + 6, 16);
  rex::memory::store_and_swap<uint32_t>(bytes + 8, 0x28280106);
  CHECK(invoke() == 1);
  CHECK(rt.kernel_state()->GetPersistedDisplay().buffer == image);
  CHECK(rt.kernel_state()->GetPersistedDisplay().width == 16);
  CHECK(rt.kernel_state()->GetPersistedDisplay().format == 0x28280106);
  // A malformed replacement must not discard the valid persisted image.
  rex::memory::store_and_swap<uint16_t>(bytes + 4, 0);
  CHECK(invoke() == 0);
  CHECK(rt.kernel_state()->GetPersistedDisplay().buffer == image);
  std::memset(bytes, 0, 12);
  CHECK(invoke() == 1);
  CHECK(rex::memory::load_and_swap<uint32_t>(bytes + 32) == image);
  CHECK(rt.kernel_state()->GetPersistedDisplay().buffer == 0);
  CHECK(invoke() == 0); // ownership cannot be transferred twice
  memory->SystemHeapFree(image);
  memory->SystemHeapFree(scratch);
}

TEST_CASE("17559 transition LUT retains channel values and reaches the original black endpoint", "[sdk][persist-display]") {
  dash17559::GammaRamp input{};
  input[0] = 31; input[1] = 32768; input[255] = 65535;
  input[256] = 16384; input[512] = 1000;
  CHECK(dash17559::TransitionRamp(input, 0) == input);
  const auto midpoint = dash17559::TransitionRamp(input, 16);
  CHECK(midpoint[0] == 15);
  CHECK(midpoint[1] == 16384);
  CHECK(midpoint[255] == 32767);
  CHECK(midpoint[256] == 8192);
  CHECK(midpoint[512] == 500);
  CHECK(dash17559::TransitionRamp(input, 32) == dash17559::GammaRamp{});
}
