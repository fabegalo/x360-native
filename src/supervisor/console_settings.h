#pragma once
#include "paths.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace x360n {
struct ConsoleLanguage { uint32_t id; const char* name; };
struct ConsoleLocale { uint32_t country; uint32_t locale; const char* name; };
// Xbox IDs, not Windows LCIDs. Language and country are independent.
struct ConsoleSettings {
  uint32_t language = 1;
  uint32_t country = 103;
  bool operator==(const ConsoleSettings&) const = default;
};
std::span<const ConsoleLanguage> ConsoleLanguages();
std::span<const ConsoleLocale> ConsoleLocales();
ConsoleSettings LoadConsoleSettings(const ProjectPaths& paths);
void SaveConsoleSettings(const ProjectPaths& paths, ConsoleSettings settings);
std::vector<std::string> SessionConsoleArguments(const std::vector<std::string>& args,
                                                ConsoleSettings settings);
}
