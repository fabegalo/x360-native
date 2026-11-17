#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <rex/kernel/export.h>
#include <rex/kernel/types/io.h>
#include "offline_achievement_fixture.h"
#include "kernel/kernel_fixture.h"
#include "xam/xam_state.h"

TEST_CASE("Original achievement tiles preserve encoded bytes and decoded ARGB output", "[sdk][xam][achievement-tiles]") {
  rex::test::KernelFixture fixture;
  auto& profile = rex::xam::CurrentXAM().GetUserProfile();
  REQUIRE(profile.CreateLocal("Tiles") == 0);
  REQUIRE(profile.SetLocalSession(profile.identity().xuid) == 0);
  // Synthetic 1x1 PNG: RGBA 11 22 33 FF.
  const std::vector<uint8_t> png{
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
      0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89,
      0x00, 0x00, 0x00, 0x10, 0x49, 0x44, 0x41, 0x54,
      0x78, 0x01, 0x01, 0x05, 0x00, 0xfa, 0xff,
      0x00, 0x11, 0x22, 0x33, 0xff, 0x02, 0x14, 0x01, 0x66,
      0xb9, 0xe1, 0xfa, 0xc1, 0x00, 0x00, 0x00, 0x00,
      0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  auto& store = rex::xam::CurrentXAM().GetOfflineAchievements();
  REQUIRE(store.ImportTitle(0x12345678, offline_achievement_fixture::Metadata(10, 2, png)) ==
          rex::xam::OfflineAchievementResult::kSuccess);
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    const auto block = c.Alloc(2048);
    REQUIRE(block);
    auto* bytes = c.GetBase() + block;
    const auto size = block + 1024, overlapped = block + 1088;
    std::memset(bytes, 0xAB, 1024);
    c.Store32(size, 0);
    CHECK(c.XAM("XamReadTile", {0, 0x12345678, 100, 0, 0, size, 0}) == 122);
    CHECK(c.Load32(size) == png.size());
    c.Store32(size, static_cast<uint32_t>(png.size() - 1));
    CHECK(c.XAM("XamReadTile", {0, 0x12345678, 100, 0, block, size, 0}) == 122);
    CHECK(std::all_of(bytes, bytes + 1024, [](auto b) { return b == 0xAB; }));
    c.Store32(size, 1024);
    CHECK(c.XAM("XamReadTile", {0, 0x12345678, 100, 0, block, size, 0}) == 0);
    CHECK(std::equal(png.begin(), png.end(), bytes));
    std::memset(bytes, 0xAB, 1024);
    CHECK(c.XAM("XamReadTileToTexture", {0, 0x12345678, 100, 0, block, 8, 2, 0}) == 0);
    CHECK(std::vector<uint8_t>(bytes, bytes + 4) == std::vector<uint8_t>{0xFF, 0x11, 0x22, 0x33});
    CHECK(std::all_of(bytes + 4, bytes + 16, [](auto b) { return b == 0; }));
    CHECK(bytes[16] == 0xAB);
    for (const auto& args : {std::initializer_list<rex::u64>{0, 0x12345678, 100, 0, block, 3, 1, 0},
                            {0, 0x12345678, 100, 0, 0xFFFFFFFC, 8, 2, 0},
                            {0, 0x12345678, 100, 0, block, 0xFFFFFFFF, 0xFFFFFFFF, 0},
                            {0, 0x12345678, 100, 0, block, 8, 2, 0xFFFFFFFC}}) {
      std::memset(bytes, 0xAB, 1024);
      const auto result = c.XAM("XamReadTileToTexture", args);
      CHECK((result == 87 || result == 122));
      CHECK(std::all_of(bytes, bytes + 1024, [](auto b) { return b == 0xAB; }));
    }
    for (const auto key : {rex::u64{999}, rex::u64{0x100000064}}) {
      std::memset(bytes, 0xAB, 1024);
      CHECK(c.XAM("XamReadTileToTexture", {0, 0x12345678, key, 0, block, 8, 2, 0}) ==
            (key == 999 ? 1168 : 87));
      CHECK(bytes[0] == 0xAB);
    }
    std::memset(c.GetBase() + overlapped, 0, 32);
    CHECK(c.XAM("XamReadTileToTexture", {0, 0x12345678, 100, 0, block, 8, 2, overlapped}) == 997);
    CHECK(c.Load32(overlapped) == 0); CHECK(c.Load32(overlapped + 24) == 0);
    std::memset(c.GetBase() + overlapped, 0, 32);
    CHECK(c.XAM("XamReadTileToTexture", {0, 0x12345678, 999, 0, block, 8, 2, overlapped}) == 997);
    CHECK(c.Load32(overlapped) == 1168);
    CHECK(c.Load32(overlapped + 24) == static_cast<uint32_t>(rex::kernel::XHRESULTFromWin32(1168)));
    // Ex keeps its two middle arguments and stack-passed OVERLAPPED.
    const auto* extended = rex::kernel::FindByOrdinal(rex::kernel::ExportModule::kXAM, 0x2FC);
    REQUIRE(extended); REQUIRE(extended->fn);
    PPCContext ctx{};
    ctx.r1.u64 = block + 1792;
    ctx.r3.u64 = 0; ctx.r4.u64 = 0x12345678; ctx.r5.u64 = 100; ctx.r6.u64 = 0;
    ctx.r7.u64 = 0xCAFE; ctx.r8.u64 = 0xBEEF; ctx.r9.u64 = block; ctx.r10.u64 = size;
    c.Store32(size, 1024); c.Store32(ctx.r1.u32 + 0x54, overlapped);
    std::memset(c.GetBase() + overlapped, 0, 32);
    extended->fn(ctx, c.GetBase());
    CHECK(ctx.r3.u32 == 997); CHECK(c.Load32(overlapped) == 0);
    CHECK(c.Load32(size) == png.size()); CHECK(std::equal(png.begin(), png.end(), bytes));
    REQUIRE(profile.SetLocalSession(0) == 0);
    CHECK(c.XAM("XamReadTileToTexture", {0, 0x12345678, 100, 0, block, 8, 2, 0}) == 1317);
  });
}
