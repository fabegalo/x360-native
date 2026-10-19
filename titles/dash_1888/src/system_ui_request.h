#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace dash1888 {
inline constexpr uint32_t kReserveApp = 0x18880001;
inline constexpr uint32_t kReleaseApp = 0x18880002;
struct Application { const char* file; uint32_t resource, size; };
inline constexpr Application kSignin{"signin.xex", 3, 16};
inline constexpr Application kCreateProfile{"createprofile.xex", 14, 16};
inline constexpr Application kGuide{"hud.xex", 0, 0};
inline constexpr Application kGamerProfile{"gamerprofile.xex", 13, 24};
inline const Application* ApplicationFor(uint32_t message) {
  switch (message) {
    case 0x21001: return &kSignin;
    case 0x2100E: return &kCreateProfile;
    case 0x21015: return &kGuide;
    case 0x21008: return &kGamerProfile;
    default: return nullptr;
  }
}
inline uint32_t Word(std::span<const uint8_t> b, size_t offset) {
  return uint32_t(b[offset]) << 24 | uint32_t(b[offset + 1]) << 16 |
         uint32_t(b[offset + 2]) << 8 | b[offset + 3];
}
// SDK requests describe the currently supported API. Translate only layouts
// proven in the 1888 exports: Signin 818E6100, Create 818E6A08, Card 818E6B28.
inline bool AdaptRequest(uint32_t message, std::span<const uint8_t> in,
                          std::vector<uint8_t>& out, uint32_t& user) {
  const auto* app = ApplicationFor(message);
  if (!app) return false;
  out.assign(in.begin(), in.end());
  if (message == 0x21001 && in.size() == 20) {
    if (Word(in, 0)) return false;
    out.assign(in.begin() + 4, in.end());
  } else if (message == 0x21015 && in.size() == 12) {
    if (Word(in, 0) || Word(in, 8)) return false;
    user = Word(in, 4); out.clear();
    return user < 4;
  } else if (message == 0x21008 && in.size() == 544) {
    if (std::any_of(in.begin() + 4, in.end(), [](uint8_t c) { return c != 0; })) return false;
    out.assign(in.begin(), in.begin() + 24);
  }
  if (out.size() != app->size) return false;
  user = message == 0x21015 ? 0 : Word(out, 0);
  return user < 4 || (message == 0x21001 && user == 0xFF);
}
}
