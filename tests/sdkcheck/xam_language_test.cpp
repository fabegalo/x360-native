#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rex/cvar.h>
#include <rex/system/flags.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <filesystem>
#include <array>
#include <algorithm>
#include "console_settings.h"

extern "C" REX_FUNC(__imp__XamGetLanguage);
extern "C" REX_FUNC(__imp__XamGetLanguageLocaleFallbackString);
extern "C" REX_FUNC(__imp__XGetLanguage);
extern "C" REX_FUNC(__imp__XTLGetLanguageV2);
extern "C" REX_FUNC(__imp__XamGetLocale);
extern "C" REX_FUNC(__imp__XamGetLocaleEx);
extern "C" REX_FUNC(__imp__ExGetXConfigSetting);
extern "C" REX_FUNC(__imp__XamGetLanguageLocaleString);
extern "C" REX_FUNC(__imp__XamGetCountry);
extern "C" REX_FUNC(__imp__XamSetCountry);
extern "C" REX_FUNC(__imp__ExSetXConfigSetting);

TEST_CASE("XamGetLanguage returns configured language instead of residual r3", "[sdk][language]") {
  const auto saved = REXCVAR_GET(user_language);
  struct Restore { uint32_t value; ~Restore() { REXCVAR_SET(user_language, value); } } restore{saved};
  auto ctx = std::make_unique<PPCContext>();
  for (uint32_t language : {1u, 2u, 9u, 12u, 17u}) {
    REXCVAR_SET(user_language, language);
    ctx->r3.u64 = 0xDEADBEEF12345678ull;
    __imp__XamGetLanguage(*ctx, nullptr);
    CHECK(ctx->r3.u32 == language);
  }
  for (uint32_t language : {0u, 10u, 18u, 0xFFFFFFFFu}) {
    REXCVAR_SET(user_language, language);
    ctx->r3.u64 = 0xDEADBEEF12345678ull;
    __imp__XamGetLanguage(*ctx, nullptr);
    CHECK(ctx->r3.u32 == 1);
  }
}

TEST_CASE("console language ABIs and XConfig agree without aliasing country to language", "[sdk][language]") {
  const auto saved_language = REXCVAR_GET(user_language), saved_country = REXCVAR_GET(user_country);
  struct Restore { uint32_t language, country; ~Restore() {
    REXCVAR_SET(user_language, language); REXCVAR_SET(user_country, country);
  } } restore{saved_language, saved_country};
  auto ctx = std::make_unique<PPCContext>();
  std::array<uint8_t, 1024> bytes{};
  for (auto [country, locale] : {std::pair{13u,4u}, {103u,36u}, {84u,27u}, {53u,20u}, {56u,21u}}) {
    REXCVAR_SET(user_country, country);
    for (uint32_t language = 1; language < 18; ++language) {
      if (language == 10) continue;
      REXCVAR_SET(user_language, language);
      const uint32_t fallback = country == 53 ? 2 : country == 56 ? 7 : 1;
      __imp__XGetLanguage(*ctx, bytes.data()); CHECK(ctx->r3.u32 == (language < 10 ? language : fallback));
      __imp__XTLGetLanguageV2(*ctx, bytes.data()); CHECK(ctx->r3.u32 == (language < 13 ? language : fallback));
      __imp__XamGetLanguage(*ctx, bytes.data()); CHECK(ctx->r3.u32 == language);
      __imp__XamGetLocale(*ctx, bytes.data()); CHECK(ctx->r3.u32 == locale);
      ctx->r3.u64=0xDEADBEEF;
      __imp__XamGetCountry(*ctx, bytes.data()); CHECK(ctx->r3.u32 == country);
      ctx->r3.u64=3; ctx->r4.u64=9; ctx->r5.u64=128; ctx->r6.u64=4; ctx->r7.u64=160;
      __imp__ExGetXConfigSetting(*ctx, bytes.data());
      CHECK(ctx->r3.u32 == 0); CHECK(bytes[128] == 0); CHECK(bytes[131] == language); CHECK(bytes[161] == 4);
      ctx->r3.u64=3; ctx->r4.u64=14; ctx->r5.u64=128; ctx->r6.u64=1; ctx->r7.u64=160;
      __imp__ExGetXConfigSetting(*ctx, bytes.data());
      CHECK(ctx->r3.u32 == 0); CHECK(bytes[128] == country); CHECK(bytes[161] == 1);
    }
  }
  REXCVAR_SET(user_language, 9); REXCVAR_SET(user_country, 13);
  ctx->r3.u64=9; ctx->r4.u64=4; ctx->r5.u64=6; ctx->r6.u64=256;
  __imp__XamGetLanguageLocaleString(*ctx, bytes.data());
  CHECK(ctx->r3.u32 == 0);
  const std::array<uint8_t,12> ptbr{0,'p',0,'t',0,'-',0,'B',0,'R',0,0};
  CHECK(std::equal(ptbr.begin(), ptbr.end(), bytes.begin()+256));
  bytes[128]=0xA5;
  ctx->r3.u64=3; ctx->r4.u64=9; ctx->r5.u64=128; ctx->r6.u64=3; ctx->r7.u64=160;
  __imp__ExGetXConfigSetting(*ctx, bytes.data());
  CHECK(ctx->r3.u32 == 0xC0000023u); CHECK(bytes[128] == 0xA5);
  // Old dashboards restrict locale IDs, without borrowing a modern dashboard.
  REXCVAR_SET(user_country,99);
  ctx->r3.u64=111; ctx->r4.u64=36;
  __imp__XamGetLocaleEx(*ctx,bytes.data()); CHECK(ctx->r3.u32 == 36);
  // Invalid country must not wrap to Brazil (13).
  REXCVAR_SET(user_country,269);
  __imp__XamGetLocale(*ctx,bytes.data()); CHECK(ctx->r3.u32 == 36);
  __imp__XamGetCountry(*ctx,bytes.data()); CHECK(ctx->r3.u32 == 103);
  for (const auto value : x360n::ConsoleLocales()) {
    REXCVAR_SET(user_country, value.country);
    __imp__XamGetLocale(*ctx,bytes.data()); CHECK(ctx->r3.u32 == value.locale);
  }
  REXCVAR_SET(user_country,13);
  ctx->r3.u64=103; __imp__XamSetCountry(*ctx,bytes.data()); CHECK(ctx->r3.u32 == 50);
  ctx->r3.u64=3; ctx->r4.u64=14; ctx->r5.u64=128; ctx->r6.u64=1;
  __imp__ExSetXConfigSetting(*ctx,bytes.data()); CHECK(ctx->r3.u32 == 0xC0000002u);
  CHECK(REXCVAR_GET(user_country) == 13);
}

TEST_CASE("XAM locale fallback is stable big endian UTF16 owned by each runtime", "[sdk][locale-fallback]") {
  using rex::X_STATUS;
  for (int lifetime = 0; lifetime < 2; ++lifetime) {
    rex::Runtime rt(std::filesystem::temp_directory_path());
    rex::RuntimeConfig config{};
    config.tool_mode = true;
    config.kernel_init = rex::kernel::InitializeKernel;
    REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
    auto ctx = std::make_unique<PPCContext>();
    for (auto [language, expected] : {std::pair{1u, u""}, {2u, u"ja-JP"},
                                    {9u, u"pt-BR"}, {17u, u"zh-CHS"}}) {
      ctx->r3.u64 = language;
      __imp__XamGetLanguageLocaleFallbackString(*ctx, rt.memory()->virtual_membase());
      const auto address = ctx->r3.u32;
      REQUIRE(address > 0x10000);
      auto* bytes = rt.memory()->TranslateVirtual(address);
      for (size_t i=0; i<=std::char_traits<char16_t>::length(expected); ++i) {
        CHECK(bytes[2*i] == uint8_t(expected[i] >> 8));
        CHECK(bytes[2*i+1] == uint8_t(expected[i]));
      }
      ctx->r3.u64 = language;
      __imp__XamGetLanguageLocaleFallbackString(*ctx, rt.memory()->virtual_membase());
      CHECK(ctx->r3.u32 == address);
    }
    ctx->r3.u64 = 18;
    __imp__XamGetLanguageLocaleFallbackString(*ctx, rt.memory()->virtual_membase());
    CHECK(ctx->r3.u32 == 0);
  }
}
