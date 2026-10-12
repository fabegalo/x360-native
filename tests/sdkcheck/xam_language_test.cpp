#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rex/cvar.h>
#include <rex/system/flags.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <filesystem>

extern "C" REX_FUNC(__imp__XamGetLanguage);
extern "C" REX_FUNC(__imp__XamGetLanguageLocaleFallbackString);

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
  for (uint32_t language : {0u, 18u, 0xFFFFFFFFu}) {
    REXCVAR_SET(user_language, language);
    ctx->r3.u64 = 0xDEADBEEF12345678ull;
    __imp__XamGetLanguage(*ctx, nullptr);
    CHECK(ctx->r3.u32 == 1);
  }
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
