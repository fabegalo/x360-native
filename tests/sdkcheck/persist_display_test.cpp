#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <rex/kernel/heap.h>
#include <rex/kernel/title_runtime.h>
#include "kernel/kernel_fixture.h"
#include "kernel/title_state.h"
#include "../../titles/dash_17559/src/display_transition.h"
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>

TEST_CASE("17559 display transition rejects a missing original frame without presenting",
          "[sdk][persist-display]") {
  rex::test::KernelFixture fixture;
  dash17559::InstallDisplayTransition(fixture.GetRuntime());
  auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
  REQUIRE(services);
  REQUIRE(services->GetSystemUI().launch_display_transition);
  fixture.RunOnGuestThread([&](rex::test::Caller&) {
    CHECK(services->GetSystemUI().launch_display_transition() == rex::kernel::XResultFromWin32(50));
    CHECK(fixture.GetRuntime().GetPersistedDisplay().buffer == 0);
  });
}

TEST_CASE("VdPersistDisplay retains the actual descriptor and transfers its allocation", "[sdk][persist-display]") {
  rex::test::KernelFixture fixture;
  auto& runtime = fixture.GetRuntime();
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    auto* memory = rex::kernel::StateOf(runtime).GetAddressSpace();
    const auto scratch = c.Alloc(64);
    const auto image = memory->SystemHeapAlloc(4096, 4096, rex::kernel::kSystemHeapPhysical);
    REQUIRE(image);
    auto* bytes = c.GetBase() + scratch;
    auto invoke = [&] { return c.Kernel("VdPersistDisplay", {scratch, scratch + 32}); };
    CHECK(invoke() == 0);
    CHECK(c.Load32(scratch + 32) == 0);
    c.Store32(scratch, image);
    rex::memory::store_and_swap<uint16_t>(bytes + 4, 16);
    rex::memory::store_and_swap<uint16_t>(bytes + 6, 16);
    c.Store32(scratch + 8, 0x28280106);
    CHECK(invoke() == 1);
    CHECK(runtime.GetPersistedDisplay().buffer == image);
    CHECK(runtime.GetPersistedDisplay().width == 16);
    CHECK(runtime.GetPersistedDisplay().height == 16);
    CHECK(runtime.GetPersistedDisplay().format == 0x28280106);
    c.Store32(image, 0x12345678);
    rex::memory::store_and_swap<uint16_t>(bytes + 4, 0);
    CHECK(invoke() == 0);
    CHECK(runtime.GetPersistedDisplay().buffer == image);
    rex::memory::store_and_swap<uint16_t>(bytes + 4, 16);
    c.Store32(scratch + 32, 0xABABABAB);
    CHECK(c.Kernel("VdPersistDisplay", {0xFFFFFFFC, scratch + 32}) == 0);
    CHECK(c.Load32(scratch + 32) == 0xABABABAB);
    CHECK(c.Kernel("VdPersistDisplay", {0, 0xFFFFFFFC}) == 0);
    CHECK(runtime.GetPersistedDisplay().buffer == image);
    CHECK(c.Kernel("VdPersistDisplay", {0, scratch + 32}) == 1);
    CHECK(c.Load32(scratch + 32) == image);
    CHECK(runtime.GetPersistedDisplay().buffer == 0);
    CHECK(c.Load32(image) == 0x12345678);
    std::memset(bytes, 0, 12);
    CHECK(invoke() == 0);
    CHECK(c.Load32(scratch + 32) == 0);
    memory->SystemHeapFree(image);
  });
  runtime.Unload();
  CHECK(runtime.GetPersistedDisplay().buffer == 0);
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
