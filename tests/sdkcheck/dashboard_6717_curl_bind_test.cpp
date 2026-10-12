// Retail 6717: xam ordinal 0xAE6 records the table dash.xex hands over and writes
// nothing back (titles/dash_6717/src/curl_bind.cpp).
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "../../titles/dash_6717/src/curl_bind.h"

extern "C" REX_FUNC(sub_92739D94);

namespace {
void PutBe32(std::vector<uint8_t>& mem, uint32_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) mem.at(at + i) = uint8_t(v >> (24 - 8 * i));
}
}  // namespace

TEST_CASE("dash 6717 xam 0xAE6 records the bound table without writing a result",
          "[sdk][dashboard-6717]") {
  // Guest memory stand-in; the table is laid out as dash.xex has it at 0x927FC914.
  std::vector<uint8_t> mem(0x1000, 0xCD);
  constexpr uint32_t kTable = 0x914;
  const uint32_t words[8] = {0x92587390, 0x925876C8, 0x92587888, 0x92587970,
                             0x92587A08, 0x92587B08, 0x92587C98, 0x00000018};
  for (uint32_t i = 0; i < 8; ++i) PutBe32(mem, kTable + 4 * i, words[i]);
  const std::vector<uint8_t> before = mem;

  auto ctx = std::make_unique<PPCContext>();
  ctx->r3.u64 = kTable;
  ctx->r4.u64 = 0x11111111;
  ctx->r5.u64 = 0x22222222;
  ctx->lr = 0x9210F1F4;
  const uint32_t calls = dash6717::LastCurlBinding().calls;

  sub_92739D94(*ctx, mem.data());

  const auto& b = dash6717::LastCurlBinding();
  CHECK(b.calls == calls + 1);
  CHECK(b.table == kTable);
  for (uint32_t i = 0; i < 8; ++i) CHECK(b.words[i] == words[i]);
  // No result is fabricated and nothing in guest memory changes.
  CHECK(ctx->r3.u64 == kTable);
  CHECK(ctx->r4.u64 == 0x11111111);
  CHECK(ctx->r5.u64 == 0x22222222);
  CHECK(mem == before);
}
