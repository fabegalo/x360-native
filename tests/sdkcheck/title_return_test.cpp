#include <catch2/catch_test_macros.hpp>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>
#include "synthetic_xex.h"
#include "../../titles/common/launch_handoff.h"
#include "../../titles/common/launch_request.h"
using rex::X_STATUS;

extern "C" REX_FUNC(__imp__XamLoaderSetLaunchData);
extern "C" REX_FUNC(__imp__XamLoaderLaunchTitle);
extern "C" REX_FUNC(__imp__XamLoaderTerminateTitle);
REXCVAR_DECLARE(std::string, x360n_handoff_in);
REXCVAR_DECLARE(std::string, x360n_handoff_out);

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
