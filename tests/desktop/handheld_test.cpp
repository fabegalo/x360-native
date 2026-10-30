// Steam Deck / Gaming Mode classification over synthetic environments and DMI
// identities. Automated only: real-hardware homologation stays separate.
#include <catch2/catch_test_macros.hpp>
#include <map>
#include "handheld.h"

using namespace x360n::desktop;
namespace {
auto Environment(std::map<std::string, std::string> values) {
  return [values = std::move(values)](const char* name) {
    const auto found = values.find(name);
    return found == values.end() ? std::string() : found->second;
  };
}
}  // namespace
TEST_CASE("Steam Deck models come from the Valve DMI identity", "[handheld]") {
  const auto none = Environment({});
  CHECK(ClassifyHandheld(none, "Valve\n", "Jupiter\n").steam_deck == SteamDeckModel::Lcd);
  CHECK(ClassifyHandheld(none, "Valve", "Galileo").steam_deck == SteamDeckModel::Oled);
  // Another vendor's board name, or another Valve product, is not a Deck.
  CHECK(ClassifyHandheld(none, "Other", "Jupiter").steam_deck == SteamDeckModel::None);
  CHECK(ClassifyHandheld(none, "Valve", "Index").steam_deck == SteamDeckModel::None);
  CHECK(ClassifyHandheld(none, "", "").steam_deck == SteamDeckModel::None);
  // Steam's own marker without a readable DMI identity: a Deck of unknown model.
  CHECK(ClassifyHandheld(Environment({{"SteamDeck", "1"}}), "", "").steam_deck == SteamDeckModel::Unknown);
  CHECK(ClassifyHandheld(Environment({{"SteamDeck", "0"}}), "", "").steam_deck == SteamDeckModel::None);
  CHECK(SteamDeckLabel(SteamDeckModel::Lcd) == "Steam Deck LCD");
  CHECK(SteamDeckLabel(SteamDeckModel::Oled) == "Steam Deck OLED");
  CHECK(SteamDeckLabel(SteamDeckModel::None).empty());
}
TEST_CASE("Gaming Mode is a gamescope session and starts the launcher fullscreen", "[handheld]") {
  const auto desktop = ClassifyHandheld(Environment({{"XDG_CURRENT_DESKTOP", "KDE"}}), "Valve", "Jupiter");
  CHECK_FALSE(desktop.gaming_mode);
  CHECK_FALSE(LauncherStartsFullscreen(desktop));  // Desktop Mode keeps a normal window.
  const auto gaming = ClassifyHandheld(Environment({{"XDG_CURRENT_DESKTOP", "gamescope"}, {"SteamGameId", "123"}}), "Valve", "Galileo");
  CHECK(gaming.gaming_mode);
  CHECK(gaming.steam_client);
  CHECK(LauncherStartsFullscreen(gaming));
  CHECK(DescribeHandheld(gaming) == "Steam Deck OLED\r\nSteamOS Gaming Mode (gamescope)\r\nSteam Input\r\n");
  // A nested gamescope client on any PC is fullscreen inside gamescope too.
  CHECK(ClassifyHandheld(Environment({{"GAMESCOPE_WAYLAND_DISPLAY", "gamescope-0"}}), "", "").gaming_mode);
  CHECK(DescribeHandheld(ClassifyHandheld(Environment({}), "", "")).empty());
}
TEST_CASE("only Linux reads a handheld session from the host", "[handheld]") {
#ifndef __linux__
  const auto detected = DetectHandheld();
  CHECK(detected.steam_deck == SteamDeckModel::None);
  CHECK_FALSE(detected.gaming_mode);
#else
  SUCCEED("Linux reads the real environment and DMI; covered by ClassifyHandheld");
#endif
}
