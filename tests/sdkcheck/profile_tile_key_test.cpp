#include <catch2/catch_test_macros.hpp>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/ppc/func.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <string_view>

extern "C" REX_FUNC(__imp__XamParseGamerTileKey);
using rex::X_RESULT;

TEST_CASE("Gamer picture keys preserve original components and reserved defaults", "[sdk][profile-tile-key]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  const auto block = rt.memory()->SystemHeapAlloc(512); REQUIRE(block);
  auto* base = rt.memory()->virtual_membase();
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
    PPCContext ctx{};
    ctx.r3.u64 = block; ctx.r4.u64 = block + 64;
    ctx.r5.u64 = block + 68; ctx.r6.u64 = block + 72;
    __imp__XamParseGamerTileKey(ctx, base);
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
  PPCContext ctx{}; ctx.r3.u64 = 5; ctx.r4.u64 = block + 64;
  ctx.r5.u64 = block + 68; ctx.r6.u64 = block + 72;
  __imp__XamParseGamerTileKey(ctx, base);
  CHECK(ctx.r3.u32 == X_ERROR_INVALID_PARAMETER); CHECK(get(64) == 0xA5A5A5A5);
  ctx.r3.u64 = block; ctx.r4.u64 = 5;
  __imp__XamParseGamerTileKey(ctx, base);
  CHECK(ctx.r3.u32 == X_ERROR_INVALID_PARAMETER);
}
