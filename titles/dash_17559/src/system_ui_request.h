#pragma once
#include <cstdint>
#include <string_view>

namespace dash17559 {
struct SystemUiApplication {
  const char* path;
  const char* name;
  uint32_t request_size;
  uint32_t user_offset;
  uint32_t resource_id;
};

// XAM 17559: 816C7378 reads the Sign-in user at +4; 816C78C4 reads
// GamerCard's at +0. Keep the original request intact, including ANY_USER (FF).
inline constexpr SystemUiApplication kSignin{
    "game:\\system\\signin.xex", "signin", 20, 4, 3};
inline constexpr SystemUiApplication kGamerCard{
    "game:\\system\\gamerprofile.xex", "gamerprofile", 544, 0, 13};
inline constexpr SystemUiApplication kCreateProfile{
    "game:\\system\\createprofile.xex", "createprofile", 136, 0, 14};
inline constexpr SystemUiApplication kGuide{
    "game:\\system\\hud.xex", "hud", 12, 4, 0};
// XAM 816C5508 packs 1148 bytes; FE/2100D at 816C7964 loads hud.xex.
// Request +8 selects the original HUD's message-box application.
inline constexpr SystemUiApplication kMessageBox{
    "game:\\system\\hud.xex", "hud message box", 1148, 4, 0};
// Original XAM 816C7230: FE/2100B, user +4, request 40 bytes.
// Original deviceselector DLL 916D6C60 registers resource ID 2.
inline constexpr SystemUiApplication kDeviceSelector{
    "game:\\system\\deviceselector.xex", "deviceselector", 40, 4, 2};
// XamAppLoad by module name (XAM 816CDD58). The Guide's direct Create Profile
// (hud 913EA808) loads createprofile.xex with no request buffer; the original
// app copies its 136-byte request only when one exists (90107850).
inline uint32_t SystemUiMessageForLoad(std::string_view module) {
  constexpr std::string_view create = "createprofile.xex";
  if (module.size() != create.size()) return 0;
  for (size_t i = 0; i < module.size(); ++i) {
    const char c = module[i] >= 'A' && module[i] <= 'Z' ? char(module[i] + 32) : module[i];
    if (c != create[i]) return 0;
  }
  return 0x2100E;
}
inline constexpr const SystemUiApplication* SystemUiApplicationFor(uint32_t message) {
  switch (message) {
    case 0x21001: return &kSignin;
    case 0x21008: return &kGamerCard;
    case 0x2100E: return &kCreateProfile;
    case 0x21015: return &kGuide;
    case 0x2100D: return &kMessageBox;
    case 0x2100B: return &kDeviceSelector;
    default: return nullptr;
  }
}
}
