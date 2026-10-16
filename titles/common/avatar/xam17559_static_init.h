#pragma once

#include <array>
#include <cstdint>

namespace x360n::avatar {

// Retail 17559 xam.xex (SHA-256 78b3dbd8...f828467) runs its C++ static
// initializers from the CRT table at 0x81A80010 when its entry point starts.
// The AOT avatar service never runs that entry point. These table entries are
// the avatar library's constant initializers, in table order: they compute
// globals such as the skeleton dequantization axis scales (0x81A975B4 and
// 0x81A975C0, (2, 0, 0) in the image) without atexit, objects or kernel calls.
inline constexpr uint32_t kXam17559AvatarInitializerTable = 0x81A801BC;
inline constexpr std::array<uint32_t, 9> kXam17559AvatarInitializers = {
    0x81A70E60, 0x81A70E78, 0x81A70E98, 0x81A70ED8, 0x81A70F00,
    0x81A70F50, 0x81A70F90, 0x81A70FB8, 0x81A71008};
inline constexpr uint32_t kXam17559SkeletonAxisScales[] = {0x81A975B4, 0x81A975C0};

}  // namespace x360n::avatar
