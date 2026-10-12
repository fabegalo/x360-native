#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xio.h>
#include <rex/system/xnotifylistener.h>
#include <rex/system/xthread.h>
#include "../../titles/dash_17559/src/launch_context.h"

extern "C" REX_FUNC(__imp__XamLaunchURI);
using rex::X_STATUS;

TEST_CASE("17559 arcade return uses the original XAM dashboard context", "[sdk][uri]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto& loader = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex")->loader_data();
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
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto listener = rex::system::object_ref<rex::system::XNotifyListener>(
      new rex::system::XNotifyListener(rt.kernel_state()));
  listener->Initialize(1, 4); // 0x80040021: system area, version 4.
  // RegisterNotifyListener queues the SDK's unrelated startup notifications.
  uint32_t startup_id = 0, startup_param = 0;
  while (listener->DequeueNotification(&startup_id, &startup_param)) {}
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  const auto address = rt.memory()->SystemHeapAlloc(2048);
  REQUIRE(address);
  auto* bytes = rt.memory()->TranslateVirtual(address);
  const auto overlap_address = address + 1600;
  auto* overlap = rt.memory()->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(overlap_address);
  uint32_t result = 0;
  auto invoke = [&](const char* uri, uint32_t flags, uint32_t user) {
    std::strcpy(reinterpret_cast<char*>(bytes), uri);
    std::memset(overlap, 0, sizeof(*overlap));
    overlap->result = 997;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(
        new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&]() {
      auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
      ctx->r3.u64 = user; ctx->r4.u64 = address; ctx->r5.u64 = flags;
      ctx->r6.u64 = overlap_address;
      __imp__XamLaunchURI(*ctx, rt.memory()->virtual_membase());
      result = ctx->r3.u32;
      return 0;
    }));
    REQUIRE(thread->Create() == X_STATUS_SUCCESS);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
  };
  invoke("gameslibrary:_RootTitle=My Games;", 5, 2);
  REQUIRE(result == 0x8000000A);
  CHECK(overlap->result == 0);
  CHECK(overlap->extended_error == 0);
  const auto& data = xam->loader_data();
  REQUIRE(data.launch_data_present);
  REQUIRE(data.launch_data.size() == 1020);
  CHECK(data.launch_data[7] == 12);
  CHECK(data.launch_data[11] == 2);
  CHECK(std::string(reinterpret_cast<const char*>(data.launch_data.data() + 12)) ==
      "pam:BuiltIn.Hub.xzp:root:BuiltIn.ContentApp.xzp:contentlibrary:PackageType=1;_RootTitle=My Games;");
  uint32_t id = 0, param = 0;
  REQUIRE(listener->DequeueNotification(&id, &param));
  CHECK(id == 0x80040021);
  CHECK(param == 0x2000C);
  const auto accepted = data.launch_data;
  for (const auto* uri : {"applibrary:test", "", "game:584108B7"}) {
    invoke(uri, 5, 0);
    CHECK((result & 0x80000000) != 0);
    CHECK(overlap->result == 997);
    CHECK_FALSE(listener->DequeueNotification(&id, &param));
    CHECK(data.launch_data == accepted);
  }
  invoke("gameslibrary:x", 0, 0);
  CHECK(result == 0x80004001);
  CHECK(overlap->result == 997);
  CHECK_FALSE(listener->DequeueNotification(&id, &param));
}
