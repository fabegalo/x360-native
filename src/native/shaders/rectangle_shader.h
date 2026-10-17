#pragma once
#include <cstdint>
#include <vector>
namespace x360n::native {
std::vector<uint8_t> CompileRectangleShader(unsigned interpolators, unsigned clip_planes);
}
