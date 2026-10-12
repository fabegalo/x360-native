#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace rex { class Runtime; }
namespace dash17559 {
using GammaRamp = std::array<uint16_t, 3 * 256>;

// Retail XAM 17559, 816C9424–816C94C4: inclusive 0..32, RGB16 * (32-step) / 32.
// This is the system routine's LUT sequence, not an application opacity effect.
inline GammaRamp TransitionRamp(const GammaRamp& original, unsigned step) {
  GammaRamp result{};
  if (step > 32) return result;
  for (size_t i = 0; i < result.size(); ++i)
    result[i] = static_cast<uint16_t>((uint32_t(original[i]) * (32 - step)) >> 5);
  return result;
}
void InstallDisplayTransition(rex::Runtime& runtime);
}
