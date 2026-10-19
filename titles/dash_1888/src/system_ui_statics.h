#pragma once
#include <array>
#include <cstdint>

namespace dash1888 {
struct UiStaticInitializer { uint32_t function, object; };
// Original CRT entries 81A9A5C8/81A9A640 in the 1888 table 81AA0054/58,
// plus the locks initialized by 81A9A7B8/81A9A7F0 (81AA0074/78).
// Call the constructor bodies without registering XAM-wide atexit handlers.
inline constexpr std::array kUiStaticInitializers{
    UiStaticInitializer{0x81935C80, 0x81AB11B4}, // XUI resource manager.
    UiStaticInitializer{0x81942E88, 0x81AB1228}, // Text service's virtual provider.
    UiStaticInitializer{0x81A9B044, 0x81AAAED4},
    UiStaticInitializer{0x81A9B044, 0x81AAAEF4},
};
}
