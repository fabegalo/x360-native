#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include <rex/kernel/title_runtime.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>
#include <rex/memory/endian.h>
#include "../../titles/dash_17559/src/system_ui_request.h"

// XAM 17559 XamShowAvatarMiniCreatorUI (816C45F8) sends FE/21018 with {user, flags}
// and 816C16C8 maps a failed dispatch to 1627. CreateProfile 90106688 calls it with
// flags 1 after a new profile; the stub left that page blank.
TEST_CASE("The avatar mini creator request reaches its original application",
          "[sdk][minicreator-request]") {
  const auto* app = dash17559::SystemUiApplicationFor(0x21018);
  REQUIRE(app);
  CHECK(std::string(app->path) == "game:\\system\\Guide.AvatarMiniCreator.xex");
  CHECK(app->request_size == 8);

  rex::test::KernelFixture fixture;
  auto* base = fixture.GetRuntime().GetMembase().GetBase();
  auto* xam = &REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI();
  uint32_t seen = 0, seen_size = 0, seen_user = ~0u, seen_flags = 0;
  uint32_t answer = 0;
  xam->system_ui_request = [&](uint32_t message, uint32_t at, uint32_t size) {
    seen = message; seen_size = size;
    seen_user = rex::memory::load_and_swap<uint32_t>(base + at);
    seen_flags = rex::memory::load_and_swap<uint32_t>(base + at + 4);
    return answer;
  };
  uint32_t accepted = ~0u, failed = ~0u;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    accepted = caller.XAM("XamShowAvatarMiniCreatorUI", {0, 1});
    answer = 0x80004005u;
    failed = caller.XAM("XamShowAvatarMiniCreatorUI", {0, 1});
  });
  CHECK(accepted == 0);
  CHECK(failed == 1627);
  CHECK(seen == 0x21018);
  CHECK(seen_size == 8);
  CHECK(seen_user == 0);
  CHECK(seen_flags == 1);
}
