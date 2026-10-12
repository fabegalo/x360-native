#include <catch2/catch_test_macros.hpp>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xexception.h>
#include "synthetic_xex.h"
#include "../../titles/common/launch_handoff.h"
#include "../../titles/common/launch_request.h"
using rex::X_STATUS;

extern "C" REX_FUNC(__imp__XamLoaderSetLaunchData);
extern "C" REX_FUNC(__imp__XamLoaderLaunchTitle);
extern "C" REX_FUNC(__imp__XamLoaderTerminateTitle);
extern "C" REX_FUNC(__imp__ExRegisterTitleTerminateNotification);
REXCVAR_DECLARE(std::string, x360n_handoff_in);
REXCVAR_DECLARE(std::string, x360n_handoff_out);

namespace {
std::vector<uint32_t> termination_calls;
uint32_t termination_registrations;
bool callbacks_before_thread_shutdown;
void TerminationCallback(PPCContext& ctx, uint8_t* base) {
  const auto address = ctx.r3.u32;
  termination_calls.push_back(address);
  callbacks_before_thread_shutdown &= !rex::Runtime::instance()->kernel_state()->is_terminating_title();
  // Original D3D callback 921B6160 removes its own registration using r3.
  ctx.r4.u64 = 0;
  __imp__ExRegisterTitleTerminateNotification(ctx, base);
  if (address == termination_registrations + 32) {
    // Removing a later registration must prevent its dispatch; routine is shared.
    ctx.r3.u64 = termination_registrations + 16;
    ctx.r4.u64 = 0;
    __imp__ExRegisterTitleTerminateNotification(ctx, base);
  }
}
}

TEST_CASE("title termination dispatches registered guest callbacks before stopping threads",
          "[sdk][title-return]") {
  x360n::test::Workdir work("x360n_title_return", "callbacks");
  rex::Runtime rt(work.dir, work.dir);
  rex::RuntimeConfig config{}; config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  REQUIRE(rt.function_dispatcher()->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
  REQUIRE(rt.function_dispatcher()->SetFunction(0x82010000, TerminationCallback));
  termination_registrations = rt.memory()->SystemHeapAlloc(64);
  REQUIRE(termination_registrations != 0);
  termination_calls.clear(); callbacks_before_thread_shutdown = true;
  auto* regs = rt.memory()->TranslateVirtual<rex::system::X_EX_TITLE_TERMINATE_REGISTRATION*>(termination_registrations);
  for (unsigned i = 0; i != 3; ++i) {
    regs[i] = {}; regs[i].notification_routine = 0x82010000; regs[i].priority = (i + 1) * 10;
  }
  bool returned = false;
  auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(), 128 * 1024, 0, [&]() {
    auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
    for (unsigned i = 0; i != 3; ++i) {
      ctx->r3.u64 = termination_registrations + i * 16; ctx->r4.u64 = 1;
      __imp__ExRegisterTitleTerminateNotification(*ctx, rt.memory()->virtual_membase());
    }
    __imp__XamLoaderTerminateTitle(*ctx, rt.memory()->virtual_membase());
    returned = true;
    return 0;
  }));
  REQUIRE(thread->Create() == X_STATUS_SUCCESS);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
  CHECK_FALSE(returned);
  CHECK(termination_calls == std::vector<uint32_t>{termination_registrations + 32, termination_registrations});
  CHECK(callbacks_before_thread_shutdown);
}

TEST_CASE("original loader return preserves flags and launch data through host handoff",
          "[sdk][title-return]") {
  x360n::test::Workdir work("x360n_title_return", "loader");
  bool terminate = false;
  uint32_t flags = 0;
  SECTION("explicit return to dashboard") {}
  SECTION("arcade return flags are preserved for supervisor validation") { flags = 0x100; }
  SECTION("title termination is distinct from bare thread return") { terminate = true; }
  rex::Runtime rt(work.dir, work.dir);
  rex::RuntimeConfig config{}; config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  const auto ptr = rt.memory()->SystemHeapAlloc(16);
  const std::vector<uint8_t> bytes{0, 1, 0xFE, 0xFF, 0x42};
  std::memcpy(rt.memory()->TranslateVirtual(ptr), bytes.data(), bytes.size());
  bool returned = false;
  auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(), 128 * 1024, 0, [&]() {
    auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
    ctx->r3.u64 = ptr; ctx->r4.u64 = bytes.size();
    __imp__XamLoaderSetLaunchData(*ctx, rt.memory()->virtual_membase());
    ctx->r3.u64 = 0; ctx->r4.u64 = flags;
    if (terminate) __imp__XamLoaderTerminateTitle(*ctx, rt.memory()->virtual_membase());
    else __imp__XamLoaderLaunchTitle(*ctx, rt.memory()->virtual_membase());
    returned = true;
    return 0;
  }));
  REQUIRE(thread->Create() == X_STATUS_SUCCESS);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
  CHECK_FALSE(returned);
  auto& loader = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex")->loader_data();
  CHECK(loader.launch_path.empty());
  CHECK(loader.launch_flags == flags);
  CHECK(loader.launch_data == bytes);
  auto file = work.dir / "return.bin";
  std::filesystem::remove(file);
  REXCVAR_SET(x360n_handoff_out, file.string());
  x360n::ExportLaunchRequest(rt);
  REXCVAR_SET(x360n_handoff_out, std::string());
  x360n::LaunchRequest request;
  REQUIRE(x360n::ReadLaunchRequest(file, request));
  CHECK(request.kind == (terminate ? x360n::LaunchRequest::TitleTerminate : x360n::LaunchRequest::Dashboard));
  CHECK(request.flags == flags);
  CHECK(request.data_present);
  CHECK(request.data == bytes);
  loader.launch_data.clear(); loader.launch_data_present = false;
  REXCVAR_SET(x360n_handoff_in, file.string());
  x360n::ImportLaunchData(rt);
  REXCVAR_SET(x360n_handoff_in, std::string());
  CHECK(x360n::IsDashboardReturn());
  CHECK(loader.launch_data == bytes);
  CHECK(loader.launch_data_present);
  CHECK(loader.launch_flags == flags);
}
