#pragma once
#include <cstdint>

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
inline constexpr const SystemUiApplication* SystemUiApplicationFor(uint32_t message) {
  switch (message) {
    case 0x21001: return &kSignin;
    case 0x21008: return &kGamerCard;
    case 0x2100E: return &kCreateProfile;
    case 0x21015: return &kGuide;
    default: return nullptr;
  }
}
}
