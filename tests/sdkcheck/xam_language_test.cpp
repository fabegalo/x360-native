#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <string>
#include <rex/kernel/kernel_settings.h>
#include <rex/kernel/thread_state.h>
#include "kernel/kernel_fixture.h"
#include "kernel/title_fixture.h"
#include "kernel/title_state.h"
#include "console_settings.h"

namespace {
template <typename Body>
void WithConsole(uint32_t language, uint32_t country, Body body) {
  rex::test::TitleBoot boot;
  rex::kernel::TitleRuntime runtime({});
  REQUIRE(runtime.GetKernelSettings().user_language.Set(language));
  REQUIRE(runtime.GetKernelSettings().user_country.Set(country));
  const auto loaded = runtime.Load();
  INFO((loaded ? std::string{} : loaded.error().GetDescription()));
  REQUIRE(loaded);
  rex::kernel::ThreadState state(1, 0x8000, 0,
      rex::kernel::StateOf(runtime).GetAddressSpace(), &runtime);
  auto* previous = rex::kernel::ThreadState::Get();
  rex::kernel::ThreadState::Bind(&state);
  rex::ScopeGuard restore([previous] { rex::kernel::ThreadState::Bind(previous); });
  rex::test::Caller caller(*state.GetContext(), runtime.GetMembase().GetBase(), runtime);
  body(caller);
}
}

TEST_CASE("XamGetLanguage returns configured language instead of residual r3", "[sdk][language]") {
  for (uint32_t language : {1u, 2u, 9u, 12u, 17u, 0u, 10u, 18u, 0xFFFFFFFFu}) {
    WithConsole(language, 103, [&](rex::test::Caller& caller) {
      caller.GetContext().r3.u64 = 0xDEADBEEF12345678ull;
      CHECK(caller.XAM("XamGetLanguage") ==
            (language > 0 && language < 18 && language != 10 ? language : 1));
    });
  }
}

TEST_CASE("console language ABIs and XConfig agree without aliasing country to language",
          "[sdk][language]") {
  for (auto [country, locale] : {std::pair{13u,4u}, {103u,36u}, {84u,27u}, {53u,20u}, {56u,21u}}) {
    for (uint32_t language = 1; language < 18; ++language) {
      if (language == 10) continue;
      WithConsole(language, country, [&](rex::test::Caller& c) {
        const uint32_t fallback = country == 53 ? 2 : country == 56 ? 7 : 1;
        CHECK(c.XAM("XGetLanguage") == (language < 10 ? language : fallback));
        CHECK(c.XAM("XTLGetLanguageV2") == (language < 13 ? language : fallback));
        CHECK(c.XAM("XamGetLanguage") == language);
        CHECK(c.XAM("XamGetLocale") == locale);
        c.GetContext().r3.u64 = 0xDEADBEEF;
        CHECK(c.XAM("XamGetCountry") == country);
        const auto value = c.Alloc(4), size = c.Alloc(2);
        CHECK(c.Kernel("ExGetXConfigSetting", {3,9,value,4,size}) == 0);
        CHECK(c.Load32(value) == language);
        CHECK(c.Load8(size) == 0);
        CHECK(c.Load8(size + 1) == 4);
        CHECK(c.Kernel("ExGetXConfigSetting", {3,14,value,1,size}) == 0);
        CHECK(c.Load8(value) == country);
        CHECK(c.Load8(size) == 0);
        CHECK(c.Load8(size + 1) == 1);
      });
    }
  }
  WithConsole(9, 13, [&](rex::test::Caller& c) {
    const auto text = c.Alloc(12), value = c.Alloc(4), size = c.Alloc(2);
    CHECK(c.XAM("XamGetLanguageLocaleString", {9,4,6,text}) == 0);
    const std::array<uint8_t,12> ptbr{0,'p',0,'t',0,'-',0,'B',0,'R',0,0};
    const auto actual = rex::test::ReadBytes(c, text, ptbr.size());
    CHECK(std::equal(ptbr.begin(), ptbr.end(), actual.begin()));
    c.Store32(value, 0xA5A5A5A5);
    CHECK(c.Kernel("ExGetXConfigSetting", {3,9,value,3,size}) == 0xC0000023u);
    CHECK(c.Load32(value) == 0xA5A5A5A5);
    // Tool mode has not frozen the settings; country is read through the official setting reference.
    REQUIRE(c.GetRuntime().GetKernelSettings().user_country.Set(99));
    CHECK(c.XAM("XamGetLocaleEx", {111,36}) == 36);
    REQUIRE(c.GetRuntime().GetKernelSettings().user_country.Set(269));
    CHECK(c.XAM("XamGetLocale") == 36);
    CHECK(c.XAM("XamGetCountry") == 103);
    CHECK(c.Kernel("ExGetXConfigSetting", {3,14,value,1,size}) == 0);
    CHECK(c.Load8(value) == 103);
    for (const auto entry : x360n::ConsoleLocales()) {
      REQUIRE(c.GetRuntime().GetKernelSettings().user_country.Set(entry.country));
      CHECK(c.XAM("XamGetLocale") == entry.locale);
    }
    REQUIRE(c.GetRuntime().GetKernelSettings().user_country.Set(13));
    CHECK(c.XAM("XamSetCountry", {103}) == 50);
    CHECK(c.Kernel("ExSetXConfigSetting", {3,14,value,1}) == 0xC0000002u);
    CHECK(c.GetRuntime().GetKernelSettings().user_country.Get() == 13);
  });
}

TEST_CASE("XAM locale fallback is stable big endian UTF16 owned by each runtime",
          "[sdk][locale-fallback]") {
  for (int lifetime = 0; lifetime < 2; ++lifetime) {
    WithConsole(1, 103, [&](rex::test::Caller& c) {
      for (auto [language, expected] : {std::pair{1u, u""}, {2u, u"ja-JP"},
                                      {9u, u"pt-BR"}, {17u, u"zh-CHS"}}) {
        const auto address = c.XAM("XamGetLanguageLocaleFallbackString", {language});
        REQUIRE(address > 0x10000);
        for (size_t i = 0; i <= std::char_traits<char16_t>::length(expected); ++i) {
          CHECK(c.Load8(address + uint32_t(2 * i)) == uint8_t(expected[i] >> 8));
          CHECK(c.Load8(address + uint32_t(2 * i + 1)) == uint8_t(expected[i]));
        }
        CHECK(c.XAM("XamGetLanguageLocaleFallbackString", {language}) == address);
      }
      CHECK(c.XAM("XamGetLanguageLocaleFallbackString", {18}) == 0);
      CHECK(c.XAM("XamGetLanguageLocaleFallbackString", {0}) == 0);
    });
  }
}
