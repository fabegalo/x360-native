#include <catch2/catch_test_macros.hpp>
#include <array>
#include <memory>
#include <string>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
extern "C" REX_FUNC(__imp__XamGetLanguageTypeface);
extern "C" REX_FUNC(__imp__XamGetLanguageTypefacePatch);

TEST_CASE("Dashboard font fallback provides bounded UTF16 locators", "[sdk][typeface]") {
  auto ctx = std::make_unique<PPCContext>();
  std::array<uint8_t, 256> bytes;
  for (auto [language, name] : {std::pair{1u, u"XenonJKLatin.xtt"},
                                {8u, u"XenonCLatin.xtt"}, {17u, u"XenonSCLatin.xtt"}}) {
    const std::u16string path = std::u16string(u"file://media:/") + name;
    bytes.fill(0xA5);
    ctx->r3.u64 = language;
    ctx->r4.u64 = path.size() + 1;
    ctx->r5.u64 = 16;
    __imp__XamGetLanguageTypeface(*ctx, bytes.data());
    CHECK(ctx->r3.u64 == 0);
    for (size_t i = 0; i < path.size(); ++i) {
      CHECK(bytes[16 + i * 2] == uint8_t(path[i] >> 8));
      CHECK(bytes[17 + i * 2] == uint8_t(path[i]));
    }
    CHECK(bytes[16 + path.size() * 2] == 0);
    CHECK(bytes[17 + path.size() * 2] == 0);
    CHECK(bytes[15] == 0xA5);
    CHECK(bytes[18 + path.size() * 2] == 0xA5);
    ctx->r3.u64 = language;
    __imp__XamGetLanguageTypefacePatch(*ctx, bytes.data());
    CHECK(ctx->r3.u64 == 0);
  }
  for (auto capacity : {0u, 1u, 28u}) {
    bytes.fill(0xA5);
    ctx->r3.u64 = 1;
    ctx->r4.u64 = capacity;
    ctx->r5.u64 = 16;
    __imp__XamGetLanguageTypeface(*ctx, bytes.data());
    CHECK((ctx->r3.u32 & 0x80000000u) != 0);
    CHECK(bytes[16] == 0xA5);
    CHECK(bytes[15] == 0xA5);
    CHECK(bytes[255] == 0xA5);
  }
}
