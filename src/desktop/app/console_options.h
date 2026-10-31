#pragma once
// Presentation only: country IDs and console persistence stay in the supervisor.
#include "app_text.h"
#include "app_controller.h"
#include <array>

namespace x360n::desktop {
struct ConsoleOption { uint32_t value; std::string name; };
// Accent-insensitive Latin search/sort, independent of the host's installed locales.
inline std::string OptionSearchKey(const std::string& text) {
  std::string result;
  const auto lower = Lower(text);
  for (size_t i = 0; i < lower.size(); ++i) {
    const auto c = static_cast<unsigned char>(lower[i]);
    if (c == 0xC3 && i + 1 < lower.size()) {
      const auto n = static_cast<unsigned char>(lower[i + 1]);
      const char folded = n >= 0xA0 && n <= 0xA5 ? 'a' : n == 0xA7 ? 'c' :
        n >= 0xA8 && n <= 0xAB ? 'e' : n >= 0xAC && n <= 0xAF ? 'i' : n == 0xB1 ? 'n' :
        n >= 0xB2 && n <= 0xB6 ? 'o' : n >= 0xB9 && n <= 0xBC ? 'u' : 0;
      if (folded) { result += folded; ++i; continue; }
    }
    result += char(c);
  }
  return result;
}
inline const std::vector<ConsoleOption>& CountryOptions() {
  struct Name { uint32_t id; const char* pt; const char* en; };
  static constexpr Name names[] = {
    {13,"Brasil (BR)","Brazil (BR)"}, {103,"Estados Unidos (US)","United States (US)"},
    {6,"Austrália (AU)","Australia (AU)"}, {5,"Áustria (AT)","Austria (AT)"},
    {8,"Bélgica (BE)","Belgium (BE)"}, {16,"Canadá (CA)","Canada (CA)"},
    {19,"Chile (CL)","Chile (CL)"}, {20,"China (CN)","China (CN)"},
    {21,"Colômbia (CO)","Colombia (CO)"}, {23,"Tchéquia (CZ)","Czechia (CZ)"},
    {25,"Dinamarca (DK)","Denmark (DK)"}, {32,"Finlândia (FI)","Finland (FI)"},
    {34,"França (FR)","France (FR)"}, {24,"Alemanha (DE)","Germany (DE)"},
    {37,"Grécia (GR)","Greece (GR)"}, {39,"Hong Kong (HK)","Hong Kong (HK)"},
    {42,"Hungria (HU)","Hungary (HU)"}, {46,"Índia (IN)","India (IN)"},
    {44,"Irlanda (IE)","Ireland (IE)"}, {50,"Itália (IT)","Italy (IT)"},
    {53,"Japão (JP)","Japan (JP)"}, {56,"Coreia do Sul (KR)","South Korea (KR)"},
    {71,"México (MX)","Mexico (MX)"}, {74,"Países Baixos (NL)","Netherlands (NL)"},
    {76,"Nova Zelândia (NZ)","New Zealand (NZ)"}, {75,"Noruega (NO)","Norway (NO)"},
    {82,"Polônia (PL)","Poland (PL)"}, {84,"Portugal (PT)","Portugal (PT)"},
    {91,"Singapura (SG)","Singapore (SG)"}, {93,"Eslováquia (SK)","Slovakia (SK)"},
    {109,"África do Sul (ZA)","South Africa (ZA)"}, {31,"Espanha (ES)","Spain (ES)"},
    {90,"Suécia (SE)","Sweden (SE)"}, {18,"Suíça (CH)","Switzerland (CH)"},
    {101,"Taiwan (TW)","Taiwan (TW)"}, {35,"Reino Unido (GB)","United Kingdom (GB)"},
    {88,"Rússia (RU)","Russia (RU)"}, {99,"Turquia (TR)","Türkiye (TR)"},
    {4,"Argentina (AR)","Argentina (AR)"}, {89,"Arábia Saudita (SA)","Saudi Arabia (SA)"},
    {45,"Israel (IL)","Israel (IL)"}, {1,"Emirados Árabes Unidos (AE)","United Arab Emirates (AE)"}};
  static const auto lists = [] {
    std::array<std::vector<ConsoleOption>, 2> values;
    for (int en = 0; en != 2; ++en) {
      for (const auto& name : names) values[en].push_back({name.id, en ? name.en : name.pt});
      std::sort(values[en].begin(), values[en].end(), [](const auto& a, const auto& b) {
        return OptionSearchKey(a.name) < OptionSearchKey(b.name);
      });
    }
    return values;
  }();
  return lists[english ? 1 : 0];
}
}  // namespace x360n::desktop
