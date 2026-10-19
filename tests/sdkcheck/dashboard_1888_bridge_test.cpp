#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <cstring>
#include <vector>
#include "../supervisor/scratch.h"
#include "../../titles/dash_1888/src/system_ui_request.h"

extern "C" REX_FUNC(__imp__XamAppRequestLoad);
extern "C" REX_FUNC(__imp__XamAppUnrequestLoad);
extern "C" REX_FUNC(__imp__XamAppLoad);
extern "C" REX_FUNC(__imp__XamShowCreateProfileUI);
extern "C" REX_FUNC(__imp__XamShowGamerCardUI);
extern "C" REX_FUNC(__imp__XamProfileOpen);
using rex::X_STATUS;

TEST_CASE("1888 native adapters copy requests and release failed app reservations", "[dashboard1888-bridge]") {
  const auto dir = UniqueTempPath("1888_bridge");
  std::filesystem::create_directories(dir);
  struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{dir};
  rex::Runtime rt(dir);
  rex::RuntimeConfig config{}; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  auto* memory = rt.memory(); auto* base = memory->virtual_membase();
  const auto path = memory->SystemHeapAlloc(256), payload = memory->SystemHeapAlloc(24);
  REQUIRE(path); REQUIRE(payload);
  bool reserved = false; unsigned releases = 0; uint32_t message = 0;
  std::vector<uint8_t> copied;
  xam->system_ui_request = [&](uint32_t msg, uint32_t request, uint32_t size) -> rex::X_HRESULT {
    if (msg == dash1888::kReserveApp) { if (reserved) return 0; reserved = true; return 1; }
    if (msg == dash1888::kReleaseApp) { reserved = false; ++releases; return 0; }
    message = msg; copied.assign(base + request, base + request + size); reserved = false; return 0;
  };
  PPCContext ctx{};
  __imp__XamAppRequestLoad(ctx, base); CHECK(ctx.r3.u32 == 1); CHECK(reserved);
  __imp__XamAppRequestLoad(ctx, base); CHECK(ctx.r3.u32 == 0);
  std::strcpy(reinterpret_cast<char*>(base + path), "createprofile.xex");
  ctx.r3.u64 = path; ctx.r4.u64 = 2; ctx.r5.u64 = ctx.r6.u64 = ctx.r7.u64 = ctx.r8.u64 = 0;
  __imp__XamAppLoad(ctx, base); CHECK(ctx.r3.u32 == 0); CHECK_FALSE(reserved);
  CHECK(message == 0x2100E); REQUIRE(copied.size() == 16); CHECK(dash1888::Word(copied, 0) == 2);
  CHECK(std::all_of(copied.begin() + 4, copied.end(), [](uint8_t b) { return b == 0; }));

  std::strcpy(reinterpret_cast<char*>(base + path), "signin.xex");
  rex::memory::store_and_swap<uint32_t>(base + payload, 1);
  rex::memory::store_and_swap<uint32_t>(base + payload + 4, 1);
  rex::memory::store_and_swap<uint32_t>(base + payload + 8, 0x420);
  ctx.r3.u64 = path; ctx.r4.u64 = 1; ctx.r5.u64 = payload; ctx.r6.u64 = 16;
  __imp__XamAppLoad(ctx, base); REQUIRE(ctx.r3.u32 == 0); CHECK(message == 0x21001);
  REQUIRE(copied.size() == 16); CHECK(std::memcmp(copied.data(), base + payload, 16) == 0);
  __imp__XamAppRequestLoad(ctx, base); REQUIRE(ctx.r3.u32 == 1);
  ctx.r3.u64 = path; ctx.r6.u64 = 15;
  __imp__XamAppLoad(ctx, base); CHECK(ctx.r3.u32 == 0x80070057u); CHECK_FALSE(reserved);

  for (const auto* file : {"friends.xex", "../17559/signin.xex", "game:\\signin.xex"}) {
    __imp__XamAppRequestLoad(ctx, base); REQUIRE(ctx.r3.u32 == 1);
    std::strcpy(reinterpret_cast<char*>(base + path), file);
    ctx.r3.u64 = path; ctx.r4.u64 = 0; ctx.r5.u64 = ctx.r6.u64 = ctx.r7.u64 = ctx.r8.u64 = 0;
    __imp__XamAppLoad(ctx, base); CHECK(ctx.r3.u32 == 0x80004001u); CHECK_FALSE(reserved);
  }
  CHECK(releases == 4);
  std::strcpy(reinterpret_cast<char*>(base + path), "hud.xex");
  ctx.r3.u64 = path; ctx.r4.u64 = 3;
  ctx.r5.u64 = ctx.r6.u64 = ctx.r7.u64 = ctx.r8.u64 = 0;
  __imp__XamAppLoad(ctx, base); REQUIRE(ctx.r3.u32 == 0);
  REQUIRE(message == 0x21015); REQUIRE(copied.size() == 12);
  std::vector<uint8_t> guide; uint32_t owner = 9;
  REQUIRE(dash1888::AdaptRequest(message, copied, guide, owner));
  CHECK(owner == 3); CHECK(guide.empty());
  // A non-null zero word is HUD's message-box contract, never Guide.
  ctx.r3.u64 = path; ctx.r5.u64 = payload; ctx.r6.u64 = 4;
  __imp__XamAppLoad(ctx, base); CHECK(ctx.r3.u32 == 0x80070057u);
  __imp__XamAppRequestLoad(ctx, base); REQUIRE(ctx.r3.u32 == 1);
  __imp__XamAppUnrequestLoad(ctx, base); CHECK(ctx.r3.u32 == 0); CHECK_FALSE(reserved);

  ctx.r3.u64 = 3; ctx.r4.u64 = 0xDEADBEEF; // The old export does not consume a modern mode.
  __imp__XamShowCreateProfileUI(ctx, base); CHECK(ctx.r3.u32 == 0);
  CHECK(message == 0x2100E); REQUIRE(copied.size() == 16); CHECK(dash1888::Word(copied, 0) == 3);
  ctx.r3.u64 = 1; __imp__XamShowGamerCardUI(ctx, base); CHECK(ctx.r3.u32 == 0);
  CHECK(message == 0x21008); CHECK(copied.size() == 24); CHECK(dash1888::Word(copied, 0) == 1);
  ctx.r3.u64 = 4; __imp__XamShowCreateProfileUI(ctx, base); CHECK(ctx.r3.u32 == 87);

  std::strcpy(reinterpret_cast<char*>(base + path), "profile:");
  ctx.r3.u64 = 0xE000000000000000ull; ctx.r4.u64 = path;
  __imp__XamProfileOpen(ctx, base); CHECK(ctx.r3.u32 == 50);
  CHECK(std::strcmp(reinterpret_cast<char*>(base + path), "profile:") == 0);
  xam->system_ui_request = {};
  ctx.r3.u64 = 0; __imp__XamShowCreateProfileUI(ctx, base); CHECK(ctx.r3.u32 != 0);
  memory->SystemHeapFree(path); memory->SystemHeapFree(payload);
}
