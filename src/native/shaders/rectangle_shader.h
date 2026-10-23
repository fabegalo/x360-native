#pragma once
#include <cstdint>
#include <vector>
namespace x360n::native {
std::vector<uint8_t> CompileRectangleShader(unsigned interpolators, unsigned clip_planes);
std::vector<uint8_t> CompilePolygonPointShader(unsigned interpolators, unsigned clip_planes, unsigned cull_planes, bool point_size);
}
