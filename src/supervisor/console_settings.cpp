#include "console_settings.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <toml++/toml.hpp>
#ifdef _WIN32
#include <windows.h>
#endif

namespace x360n {
namespace {
// XAM language/locale tables of the pinned runtime. ID 10 is reserved.
constexpr ConsoleLanguage languages[] = {
    {9, "Português (Brasil)"}, {1, "English"}, {2, "日本語"}, {3, "Deutsch"},
    {4, "Français"}, {5, "Español"}, {6, "Italiano"}, {7, "한국어"},
    {8, "繁體中文"}, {11, "Polski"}, {12, "Русский"}, {13, "Svenska"},
    {14, "Türkçe"}, {15, "Norsk (bokmål)"}, {16, "Nederlands"}, {17, "简体中文"}};
constexpr ConsoleLocale locales[] = {
    {13,4,"Brasil (BR)"}, {103,36,"United States (US)"}, {6,1,"Australia (AU)"},
    {5,2,"Österreich (AT)"}, {8,3,"Belgique / België (BE)"}, {16,5,"Canada (CA)"},
    {19,6,"Chile (CL)"}, {20,7,"中国 (CN)"}, {21,8,"Colombia (CO)"},
    {23,9,"Česko (CZ)"}, {25,10,"Danmark (DK)"}, {32,11,"Suomi (FI)"},
    {34,12,"France (FR)"}, {24,13,"Deutschland (DE)"}, {37,14,"Ελλάδα (GR)"},
    {39,15,"香港 (HK)"}, {42,16,"Magyarország (HU)"}, {46,17,"India (IN)"},
    {44,18,"Ireland (IE)"}, {50,19,"Italia (IT)"}, {53,20,"日本 (JP)"},
    {56,21,"대한민국 (KR)"}, {71,22,"México (MX)"}, {74,23,"Nederland (NL)"},
    {76,24,"New Zealand (NZ)"}, {75,25,"Norge (NO)"}, {82,26,"Polska (PL)"},
    {84,27,"Portugal (PT)"}, {91,28,"Singapore (SG)"}, {93,29,"Slovensko (SK)"},
    {109,30,"South Africa (ZA)"}, {31,31,"España (ES)"}, {90,32,"Sverige (SE)"},
    {18,33,"Schweiz / Suisse (CH)"}, {101,34,"台灣 (TW)"}, {35,35,"United Kingdom (GB)"},
    {88,37,"Россия (RU)"}, {99,39,"Türkiye (TR)"}, {4,40,"Argentina (AR)"},
    {89,41,"Saudi Arabia (SA)"}, {45,42,"Israel (IL)"}, {1,43,"United Arab Emirates (AE)"}};
void Validate(ConsoleSettings settings) {
  if (std::ranges::none_of(languages, [&](auto v) { return v.id == settings.language; }) ||
      std::ranges::none_of(locales, [&](auto v) { return v.country == settings.country; }))
    throw std::runtime_error("Invalid console language or country; choose a supported Xbox 360 value");
}
}
std::span<const ConsoleLanguage> ConsoleLanguages() { return languages; }
std::span<const ConsoleLocale> ConsoleLocales() { return locales; }
ConsoleSettings LoadConsoleSettings(const ProjectPaths& paths) {
  const auto file = paths.local() / "console.toml";
  if (!std::filesystem::exists(file)) return {};
  const auto table = toml::parse_file(PathUtf8(file));
  const auto version = table["version"].value<int>();
  const auto language = table["language"].value<uint32_t>();
  const auto country = table["country"].value<uint32_t>();
  if (version != 1 || !language || !country)
    throw std::runtime_error("Invalid console preferences: " + PathUtf8(file));
  ConsoleSettings settings{*language, *country};
  Validate(settings);
  return settings;
}
void SaveConsoleSettings(const ProjectPaths& paths, ConsoleSettings settings) {
  Validate(settings);
  const auto file = paths.local() / "console.toml";
  auto temp = file; temp += ".tmp";
  std::filesystem::create_directories(file.parent_path());
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << "version = 1\nlanguage = " << settings.language << "\ncountry = " << settings.country << '\n';
    out.close();
    if (!out) throw std::runtime_error("Cannot persist console preferences");
  }
#ifdef _WIN32
  if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot replace console preferences: Windows error " + std::to_string(GetLastError()));
#else
  std::filesystem::rename(temp, file);
#endif
}
std::vector<std::string> SessionConsoleArguments(const std::vector<std::string>& args,
                                                ConsoleSettings settings) {
  Validate(settings);
  std::vector<std::string> result;
  for (size_t i = 0; i < args.size(); ++i) {
    auto arg = args[i];
    if (arg == "--") throw std::runtime_error("Session manifests cannot terminate option parsing");
    std::replace(arg.begin(), arg.end(), '-', '_');
    if (arg == "__user_language" || arg == "__user_country") {
      if (++i == args.size() || args[i].starts_with("--"))
        throw std::runtime_error("Missing manifest console setting value");
      continue;
    }
    if (arg.starts_with("__user_language=") || arg.starts_with("__user_country=")) continue;
    result.push_back(args[i]);
  }
  result.push_back("--user_language=" + std::to_string(settings.language));
  result.push_back("--user_country=" + std::to_string(settings.country));
  return result;
}
}
