#pragma once
#include <cstdint>
namespace x360n::forza {
// Call only after the three original XEX digests in main.cpp have matched.
void ConfigureEnhancedTiming();
void ResizeEnhancedLayout(uint32_t width, uint32_t height);
}
