#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include "kernel/kernel_fixture.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/guest_memory.h>
#include <rex/kernel/object/notification_broadcaster.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/types/io.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/loader_state.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/memory/endian.h>
#include "../../titles/dash_17559/src/launch_context.h"

#include "../../titles/dash_17559/src/system_ui_dependencies.h"
#include "../../titles/dash_17559/src/system_ui.h"

TEST_CASE("17559 AOT modules read the host-owned verified XAM handle", "[sdk][uri]") {
  auto& registry = REX_SETTINGS();
  REQUIRE_FALSE(registry.Find("dash17559.ui_self"));
  CHECK(dash17559::OriginalUiModuleHandle() == 0);
  REQUIRE(dash17559::RegisterSystemUiSettings(registry));
  rex::ScopeGuard unregister([&] { registry.Unregister(dash17559::GetSystemUiSettings()); });
  auto& setting = dash17559::GetSystemUiSettings().ui_self;
  REQUIRE(setting.SetText(rex::SettingLayer::kRuntime, "2174898176"));
  CHECK(dash17559::OriginalUiModuleHandle() == 2174898176u);
  REQUIRE(setting.Reset());
  CHECK(dash17559::OriginalUiModuleHandle() == 0);
}

extern "C" REX_FUNC(__imp__XamLaunchURI);

TEST_CASE("17559 arcade return uses the original XAM dashboard context", "[sdk][uri]") {
  rex::test::KernelFixture fixture;
  auto& rt = fixture.GetRuntime();
  auto& loader = REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState();
  const std::vector<uint8_t> original{1, 0, 0xFF, 4};
  loader.launch_data = original; loader.launch_data_present = true;
  CHECK(dash17559::PrepareReturnContext(rt));
  CHECK(loader.launch_data == original);
  loader.launch_flags = 0x200;
  CHECK_FALSE(dash17559::PrepareReturnContext(rt));
  CHECK(loader.launch_data == original);
  loader.launch_flags = 0x100;
  CHECK(dash17559::PrepareReturnContext(rt));
  CHECK(loader.launch_flags == 0x100);
  REQUIRE(loader.launch_data.size() == 1020);
  CHECK(loader.launch_data_present);
  CHECK(rex::memory::load_and_swap<uint32_t>(loader.launch_data.data()) == 0);
  CHECK(rex::memory::load_and_swap<uint32_t>(loader.launch_data.data() + 4) == 23);
  CHECK(rex::memory::load_and_swap<uint32_t>(loader.launch_data.data() + 8) == 0xFF);
  CHECK(std::all_of(loader.launch_data.begin() + 12, loader.launch_data.end(), [](uint8_t b) { return b == 0; }));
}

TEST_CASE("17559 library URI delivers launch data before acknowledging the request", "[sdk][uri]") {
  rex::test::KernelFixture fixture;
  auto& rt = fixture.GetRuntime();
  auto listener = rt.GetNotificationBroadcaster().AddNotifyListener(1, 4);
  REQUIRE(listener);
  rex::ScopeGuard close([&] { CHECK(rt.GetObjectTable().Close(*listener)); });
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
  const auto notification = caller.Alloc(8);
  auto dequeue = [&](uint32_t* id, uint32_t* param) {
    if (!caller.XAM("XNotifyGetNext", {*listener, 0, notification, notification + 4})) return false;
    *id = caller.Load32(notification); *param = caller.Load32(notification + 4);
    return true;
  };
  // RegisterNotifyListener queues the SDK's unrelated startup notifications.
  uint32_t startup_id = 0, startup_param = 0;
  while (dequeue(&startup_id, &startup_param)) {}
  auto* xam = REX_INTERFACE(rex::xam::IXAMTitleServices);
  const auto address = caller.Alloc(2048);
  REQUIRE(address);
  auto* bytes = caller.GetBase() + address;
  const auto overlap_address = address + 1600;
  auto* overlap = reinterpret_cast<rex::kernel::XOVERLAPPED*>(caller.GetBase() + overlap_address);
  uint32_t result = 0;
  auto invoke = [&](const char* uri, uint32_t flags, uint32_t user) {
    REQUIRE(std::strlen(uri) < 1600);
    std::memcpy(bytes, uri, std::strlen(uri) + 1);
    std::memset(overlap, 0, sizeof(*overlap));
    overlap->result = 997;
      auto* ctx = &caller.GetContext();
      ctx->r3.u64 = user; ctx->r4.u64 = address; ctx->r5.u64 = flags;
      ctx->r6.u64 = overlap_address;
      __imp__XamLaunchURI(*ctx, caller.GetBase());
      result = ctx->r3.u32;
  };
  invoke("gameslibrary:_RootTitle=My Games;", 5, 2);
  REQUIRE(result == 0x8000000A);
  CHECK(overlap->result == 0);
  CHECK(overlap->extended_error == 0);
  const auto& data = xam->GetLoaderState();
  REQUIRE(data.launch_data_present);
  REQUIRE(data.launch_data.size() == 1020);
  CHECK(data.launch_data[7] == 12);
  CHECK(data.launch_data[11] == 2);
  CHECK(std::string(reinterpret_cast<const char*>(data.launch_data.data() + 12)) ==
      "pam:BuiltIn.Hub.xzp:root:BuiltIn.ContentApp.xzp:contentlibrary:PackageType=1;_RootTitle=My Games;");
  uint32_t id = 0, param = 0;
  REQUIRE(dequeue(&id, &param));
  CHECK(id == 0x80040021);
  CHECK(param == 0x2000C);
  const auto accepted = data.launch_data;
  for (const auto* uri : {"applibrary:test", "", "game:584108B7"}) {
    invoke(uri, 5, 0);
    CHECK((result & 0x80000000) != 0);
    CHECK(overlap->result == 997);
    CHECK_FALSE(dequeue(&id, &param));
    CHECK(data.launch_data == accepted);
  }
  invoke("gameslibrary:x", 0, 0);
  CHECK(result == 0x80004001);
  CHECK(overlap->result == 997);
  CHECK_FALSE(dequeue(&id, &param));
  // Bad guest ranges must not publish launch data, notifications or completion.
  for (const auto args : {std::array<uint32_t, 3>{0, UINT32_MAX, overlap_address},
                          std::array<uint32_t, 3>{0, address, UINT32_MAX - 15},
                          std::array<uint32_t, 3>{4, address, overlap_address},
                          std::array<uint32_t, 3>{0, 0, overlap_address}}) {
    auto& ctx = caller.GetContext();
    ctx.r3.u64 = args[0]; ctx.r4.u64 = args[1]; ctx.r5.u64 = 5; ctx.r6.u64 = args[2];
    __imp__XamLaunchURI(ctx, caller.GetBase());
    CHECK(ctx.r3.u32 == 0x80070057);
    CHECK(overlap->result == 997);
    CHECK(data.launch_data == accepted);
    CHECK_FALSE(dequeue(&id, &param));
  }
  });
}
