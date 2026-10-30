#pragma once
// Steam Deck (LCD/OLED) and SteamOS Gaming Mode detection for the Linux x64
// package: the same package, adapted only where the session requires it.
// ClassifyHandheld is a pure decision over injected inputs (unit-tested);
// DetectHandheld reads this process's environment and the DMI identity.
#include <functional>
#include <string>

namespace x360n::desktop {
// Unknown: Steam reports a Deck, but its DMI identity is unreadable.
enum class SteamDeckModel { None, Unknown, Lcd, Oled };
struct HandheldEnvironment {
  SteamDeckModel steam_deck = SteamDeckModel::None;
  // A gamescope client: SteamOS Gaming Mode shows exactly one fullscreen app.
  bool gaming_mode = false;
  // Started by the Steam client, so Steam Input owns the built-in controls and
  // presents them as a standard gamepad.
  bool steam_client = false;
};
// env returns "" for an unset variable. dmi_vendor/dmi_product are the contents of
// /sys/class/dmi/id/sys_vendor and product_name (Valve Jupiter: LCD; Galileo: OLED).
HandheldEnvironment ClassifyHandheld(const std::function<std::string(const char*)>& env,
                                     std::string dmi_vendor, std::string dmi_product);
// Linux only; every other system reports no handheld session.
HandheldEnvironment DetectHandheld();
// "Steam Deck LCD", "Steam Deck OLED", "Steam Deck" or "".
std::string SteamDeckLabel(SteamDeckModel model);
// Diagnostics lines (CRLF-terminated, like Environment::description); "" when none applies.
std::string DescribeHandheld(const HandheldEnvironment& environment);
// gamescope composites one fullscreen surface; a smaller window would be letterboxed.
inline bool LauncherStartsFullscreen(const HandheldEnvironment& environment) { return environment.gaming_mode; }
}  // namespace x360n::desktop
