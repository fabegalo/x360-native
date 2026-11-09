#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <filesystem>
#include <string>
#include "../../titles/dash_17559/src/system_ui_request.h"

extern "C" REX_FUNC(__imp__XamShowAvatarMiniCreatorUI);

// XAM 17559 XamShowAvatarMiniCreatorUI (816C45F8) sends FE/21018 with {user, flags}
// and 816C16C8 maps a failed dispatch to 1627. CreateProfile 90106688 calls it with
// flags 1 after a new profile; the stub left that page blank.
TEST_CASE("The avatar mini creator request reaches its original application",
          "[sdk][minicreator-request]") {
  const auto* app = dash17559::SystemUiApplicationFor(0x21018);
  REQUIRE(app);
  CHECK(std::string(app->path) == "game:\\system\\Guide.AvatarMiniCreator.xex");
  CHECK(app->request_size == 8);

  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto* base = rt.memory()->virtual_membase();
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam);
  uint32_t seen = 0, seen_size = 0, seen_user = ~0u, seen_flags = 0;
  rex::X_HRESULT answer = 0;
  xam->system_ui_request = [&](uint32_t message, uint32_t at, uint32_t size) {
    seen = message; seen_size = size;
    seen_user = rex::memory::load_and_swap<uint32_t>(base + at);
    seen_flags = rex::memory::load_and_swap<uint32_t>(base + at + 4);
    return answer;
  };
  uint32_t accepted = ~0u, failed = ~0u;
  auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(), 128 * 1024, 0, [&] {
    auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
    ctx->r3.u64 = 0; ctx->r4.u64 = 1;
    __imp__XamShowAvatarMiniCreatorUI(*ctx, base);
    accepted = ctx->r3.u32;
    answer = rex::X_HRESULT(0x80004005u);
    ctx->r3.u64 = 0; ctx->r4.u64 = 1;
    __imp__XamShowAvatarMiniCreatorUI(*ctx, base);
    failed = ctx->r3.u32;
    return 0;
  }));
  REQUIRE(caller->Create() == 0); uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(caller->Wait(0, 0, 0, &timeout) == 0);
  CHECK(accepted == 0);
  CHECK(failed == 1627);
  CHECK(seen == 0x21018);
  CHECK(seen_size == 8);
  CHECK(seen_user == 0);
  CHECK(seen_flags == 1);
}
