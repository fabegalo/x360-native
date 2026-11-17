#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include <rex/kernel/types/status.h>
#include <rex/memory/endian.h>
#include <array>
#include <cstring>
#include <string_view>

TEST_CASE("Gamer picture keys preserve original components and reserved defaults", "[sdk][profile-tile-key]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([](rex::test::Caller& caller) {
  const auto block = caller.Alloc(512); REQUIRE(block);
  auto* base = caller.GetBase();
  auto put = [&](uint32_t offset, uint32_t value) {
    rex::memory::store_and_swap<uint32_t>(base + block + offset, value);
  };
  auto get = [&](uint32_t offset) {
    return rex::memory::load_and_swap<uint32_t>(base + block + offset);
  };
  auto run = [&](std::u16string_view text, bool null_buffer = false) {
    std::memset(base + block, 0, 512);
    put(12, null_buffer ? 0 : block + 128);
    for (size_t i = 0; i < text.size(); ++i)
      rex::memory::store_and_swap<uint16_t>(base + block + 128 + i * 2, text[i]);
    auto& ctx = caller.GetContext();
    ctx.r3.u64 = block; ctx.r4.u64 = block + 64;
    ctx.r5.u64 = block + 68; ctx.r6.u64 = block + 72;
    caller.XAM("XamParseGamerTileKey");
    CHECK(ctx.r3.u32 == 0);
    return std::array<uint32_t, 3>{get(64), get(68), get(72)};
  };
  REQUIRE(run(u"584108B70000000200000003") == std::array<uint32_t, 3>{0x584108B7, 2, 3});
  CHECK(get(8) == 50);
  CHECK(run(u"ffffffff0123ABCDfedcba98") == std::array<uint32_t, 3>{0xFFFFFFFF, 0x0123ABCD, 0xFEDCBA98});
  const std::array<uint32_t, 3> original_default{0xFFFE07D1, 0x20000, 0x10000};
  CHECK(run(u"FFFE08540000000200000003") == original_default);
  CHECK(run(u"0001ABCD0000000200000003") == original_default);
  CHECK(run(u"0002ABCD0000000200000003") == original_default);
  CHECK(run({}, true) == original_default); CHECK(get(8) == 0);
  CHECK(run(u"000000000000000000000000") == std::array<uint32_t, 3>{0, 0, 0});
  put(64, 0xA5A5A5A5);
  auto& ctx = caller.GetContext(); ctx.r3.u64 = 5; ctx.r4.u64 = block + 64;
  ctx.r5.u64 = block + 68; ctx.r6.u64 = block + 72;
  caller.XAM("XamParseGamerTileKey");
  CHECK(ctx.r3.u32 == rex::kernel::X_ERROR_INVALID_PARAMETER); CHECK(get(64) == 0xA5A5A5A5);
  ctx.r3.u64 = block; ctx.r4.u64 = 5;
  caller.XAM("XamParseGamerTileKey");
  CHECK(ctx.r3.u32 == rex::kernel::X_ERROR_INVALID_PARAMETER);
  });
}
