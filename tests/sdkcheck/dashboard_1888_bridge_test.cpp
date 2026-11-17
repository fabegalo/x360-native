#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/heap.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>
#include <rex/ppc/recompiled_code.h>
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
namespace {
void Call(rex::test::KernelFixture& fixture,PPCContext& args,uint8_t* base,
          void (*function)(PPCContext&,uint8_t*)) {
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& context=caller.GetContext();
    context.r3=args.r3;context.r4=args.r4;context.r5=args.r5;context.r6=args.r6;
    context.r7=args.r7;context.r8=args.r8;context.r9=args.r9;context.r10=args.r10;
    function(context,base);args=context;
  });
}
}

TEST_CASE("1888 native adapters copy requests and release failed app reservations", "[dashboard1888-bridge]") {
  rex::test::KernelFixture fixture;
  auto& rt=fixture.GetRuntime();
  auto* memory=&rex::kernel::GetAddressSpace(rt);
  auto* base=memory->GetVirtualMembase();
  auto* xam=&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI();
  const auto path = memory->SystemHeapAlloc(256), payload = memory->SystemHeapAlloc(24);
  REQUIRE(path); REQUIRE(payload);
  bool reserved = false; unsigned releases = 0; uint32_t message = 0;
  std::vector<uint8_t> copied;
  xam->system_ui_request = [&](uint32_t msg, uint32_t request, uint32_t size) -> uint32_t {
    if (msg == dash1888::kReserveApp) { if (reserved) return 0; reserved = true; return 1; }
    if (msg == dash1888::kReleaseApp) { reserved = false; ++releases; return 0; }
    message = msg; copied.assign(base + request, base + request + size); reserved = false; return 0;
  };
  PPCContext ctx{};
  Call(fixture,ctx,base,__imp__XamAppRequestLoad); CHECK(ctx.r3.u32 == 1); CHECK(reserved);
  Call(fixture,ctx,base,__imp__XamAppRequestLoad); CHECK(ctx.r3.u32 == 0);
  std::strcpy(reinterpret_cast<char*>(base + path), "createprofile.xex");
  ctx.r3.u64 = path; ctx.r4.u64 = 2; ctx.r5.u64 = ctx.r6.u64 = ctx.r7.u64 = ctx.r8.u64 = 0;
  Call(fixture,ctx,base,__imp__XamAppLoad); CHECK(ctx.r3.u32 == 0); CHECK_FALSE(reserved);
  CHECK(message == 0x2100E); REQUIRE(copied.size() == 16); CHECK(dash1888::Word(copied, 0) == 2);
  CHECK(std::all_of(copied.begin() + 4, copied.end(), [](uint8_t b) { return b == 0; }));

  std::strcpy(reinterpret_cast<char*>(base + path), "signin.xex");
  rex::memory::store_and_swap<uint32_t>(base + payload, 1);
  rex::memory::store_and_swap<uint32_t>(base + payload + 4, 1);
  rex::memory::store_and_swap<uint32_t>(base + payload + 8, 0x420);
  ctx.r3.u64 = path; ctx.r4.u64 = 1; ctx.r5.u64 = payload; ctx.r6.u64 = 16;
  Call(fixture,ctx,base,__imp__XamAppLoad); REQUIRE(ctx.r3.u32 == 0); CHECK(message == 0x21001);
  REQUIRE(copied.size() == 16); CHECK(std::memcmp(copied.data(), base + payload, 16) == 0);
  Call(fixture,ctx,base,__imp__XamAppRequestLoad); REQUIRE(ctx.r3.u32 == 1);
  ctx.r3.u64 = path; ctx.r6.u64 = 15;
  Call(fixture,ctx,base,__imp__XamAppLoad); CHECK(ctx.r3.u32 == 0x80070057u); CHECK_FALSE(reserved);

  for (const auto* file : {"friends.xex", "../17559/signin.xex", "game:\\signin.xex"}) {
    Call(fixture,ctx,base,__imp__XamAppRequestLoad); REQUIRE(ctx.r3.u32 == 1);
    std::strcpy(reinterpret_cast<char*>(base + path), file);
    ctx.r3.u64 = path; ctx.r4.u64 = 0; ctx.r5.u64 = ctx.r6.u64 = ctx.r7.u64 = ctx.r8.u64 = 0;
    Call(fixture,ctx,base,__imp__XamAppLoad); CHECK(ctx.r3.u32 == 0x80004001u); CHECK_FALSE(reserved);
  }
  CHECK(releases == 4);
  std::strcpy(reinterpret_cast<char*>(base + path), "hud.xex");
  ctx.r3.u64 = path; ctx.r4.u64 = 3;
  ctx.r5.u64 = ctx.r6.u64 = ctx.r7.u64 = ctx.r8.u64 = 0;
  Call(fixture,ctx,base,__imp__XamAppLoad); REQUIRE(ctx.r3.u32 == 0);
  REQUIRE(message == 0x21015); REQUIRE(copied.size() == 12);
  std::vector<uint8_t> guide; uint32_t owner = 9;
  REQUIRE(dash1888::AdaptRequest(message, copied, guide, owner));
  CHECK(owner == 3); CHECK(guide.empty());
  // A non-null zero word is HUD's message-box contract, never Guide.
  ctx.r3.u64 = path; ctx.r5.u64 = payload; ctx.r6.u64 = 4;
  Call(fixture,ctx,base,__imp__XamAppLoad); CHECK(ctx.r3.u32 == 0x80070057u);
  Call(fixture,ctx,base,__imp__XamAppRequestLoad); REQUIRE(ctx.r3.u32 == 1);
  Call(fixture,ctx,base,__imp__XamAppUnrequestLoad); CHECK(ctx.r3.u32 == 0); CHECK_FALSE(reserved);

  ctx.r3.u64 = 3; ctx.r4.u64 = 0xDEADBEEF; // The old export does not consume a modern mode.
  Call(fixture,ctx,base,__imp__XamShowCreateProfileUI); CHECK(ctx.r3.u32 == 0);
  CHECK(message == 0x2100E); REQUIRE(copied.size() == 16); CHECK(dash1888::Word(copied, 0) == 3);
  ctx.r3.u64 = 1; Call(fixture,ctx,base,__imp__XamShowGamerCardUI); CHECK(ctx.r3.u32 == 0);
  CHECK(message == 0x21008); CHECK(copied.size() == 24); CHECK(dash1888::Word(copied, 0) == 1);
  ctx.r3.u64 = 4; Call(fixture,ctx,base,__imp__XamShowCreateProfileUI); CHECK(ctx.r3.u32 == 87);

  std::strcpy(reinterpret_cast<char*>(base + path), "profile:");
  ctx.r3.u64 = 0xE000000000000000ull; ctx.r4.u64 = path;
  Call(fixture,ctx,base,__imp__XamProfileOpen); CHECK(ctx.r3.u32 == 50);
  CHECK(std::strcmp(reinterpret_cast<char*>(base + path), "profile:") == 0);
  xam->system_ui_request = {};
  ctx.r3.u64 = 0; Call(fixture,ctx,base,__imp__XamShowCreateProfileUI); CHECK(ctx.r3.u32 != 0);
  memory->SystemHeapFree(path); memory->SystemHeapFree(payload);
}
