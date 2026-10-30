#include "handheld.h"
#include <cstdlib>
#include <fstream>

namespace x360n::desktop {
namespace {
std::string Trim(std::string text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
[[maybe_unused]] std::string ReadFirstLine(const char* path) {
  std::ifstream file(path);
  std::string line;
  std::getline(file, line);
  return line;
}
}  // namespace
HandheldEnvironment ClassifyHandheld(const std::function<std::string(const char*)>& env,
                                     std::string dmi_vendor, std::string dmi_product) {
  HandheldEnvironment result;
  dmi_vendor = Trim(std::move(dmi_vendor));
  dmi_product = Trim(std::move(dmi_product));
  if (dmi_vendor == "Valve" && dmi_product == "Jupiter") result.steam_deck = SteamDeckModel::Lcd;
  else if (dmi_vendor == "Valve" && dmi_product == "Galileo") result.steam_deck = SteamDeckModel::Oled;
  else if (env("SteamDeck") == "1") result.steam_deck = SteamDeckModel::Unknown;
  result.gaming_mode = env("XDG_CURRENT_DESKTOP") == "gamescope" || !env("GAMESCOPE_WAYLAND_DISPLAY").empty();
  result.steam_client = !env("SteamGameId").empty() || !env("SteamAppId").empty();
  return result;
}
HandheldEnvironment DetectHandheld() {
#ifdef __linux__
  const auto env = [](const char* name) {
    const char* value = std::getenv(name);
    return std::string(value ? value : "");
  };
  return ClassifyHandheld(env, ReadFirstLine("/sys/class/dmi/id/sys_vendor"), ReadFirstLine("/sys/class/dmi/id/product_name"));
#else
  return {};
#endif
}
std::string SteamDeckLabel(SteamDeckModel model) {
  switch (model) {
    case SteamDeckModel::Lcd: return "Steam Deck LCD";
    case SteamDeckModel::Oled: return "Steam Deck OLED";
    case SteamDeckModel::Unknown: return "Steam Deck";
    case SteamDeckModel::None: break;
  }
  return {};
}
std::string DescribeHandheld(const HandheldEnvironment& environment) {
  std::string text;
  if (environment.steam_deck != SteamDeckModel::None) text += SteamDeckLabel(environment.steam_deck) + "\r\n";
  if (environment.gaming_mode) text += "SteamOS Gaming Mode (gamescope)\r\n";
  if (environment.steam_client) text += "Steam Input\r\n";
  return text;
}
}  // namespace x360n::desktop
