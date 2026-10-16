#include "console_settings.h"
#include "scratch.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <set>
using namespace x360n;

TEST_CASE("console preferences persist independent language and locale across reload", "[console-settings]") {
  const auto root = PathFromUtf8(X360N_SOURCE_ROOT) / ".local/tmp" / UniqueTempPath("console").filename();
  ProjectPaths paths{root};
  CHECK(LoadConsoleSettings(paths) == ConsoleSettings{1, 103});
  for (const auto settings : {ConsoleSettings{9, 13}, {1, 13}, {9, 103}, {17, 20}}) {
    SaveConsoleSettings(paths, settings);
    CHECK(LoadConsoleSettings(paths) == settings);
  }
  const auto session = LoadConsoleSettings(paths);
  SaveConsoleSettings(paths, {9, 13});
  CHECK(session == ConsoleSettings{17, 20});
  for (const auto invalid : {ConsoleSettings{0, 13}, {10, 13}, {18, 13}, {9, 0}, {9, 269}})
    CHECK_THROWS(SaveConsoleSettings(paths, invalid));
  CHECK(LoadConsoleSettings(paths) == ConsoleSettings{9, 13});
  for (auto text : {"version=1\nlanguage=9\n", "version=2\nlanguage=9\ncountry=13\n",
                    "version=1\nlanguage=-1\ncountry=13\n", "version=1\nlanguage=1\ncountry=999\n"}) {
    std::ofstream(paths.local() / "console.toml") << text;
    CHECK_THROWS(LoadConsoleSettings(paths));
  }
  std::filesystem::remove_all(root);
}
TEST_CASE("console settings replace manifest overrides for every session child", "[console-settings]") {
  CHECK(SessionConsoleArguments({"--user_language=1", "--user-country", "103", "--foo=bar",
                                  "--user-language", "2", "--user_country=53"}, {9, 13}) ==
        std::vector<std::string>{"--foo=bar", "--user_language=9", "--user_country=13"});
  CHECK_THROWS(SessionConsoleArguments({"--user_language"}, {}));
  CHECK_THROWS(SessionConsoleArguments({"--user_country", "--foo"}, {}));
  CHECK_THROWS(SessionConsoleArguments({"--"}, {}));
  std::set<uint32_t> languages, countries, locales;
  for (auto v : ConsoleLanguages()) CHECK(languages.insert(v.id).second);
  for (auto v : ConsoleLocales()) {
    CHECK(countries.insert(v.country).second); CHECK(locales.insert(v.locale).second);
    for (auto language : languages) CHECK_NOTHROW(SessionConsoleArguments({}, {language, v.country}));
  }
  CHECK(languages.size() == 16);
  CHECK(countries.size() == 42);
  CHECK_FALSE(languages.contains(10)); CHECK_FALSE(locales.contains(38));
}
