#pragma once
#include <rex/xenos/ucode.h>
#include <cstdint>
#include <vector>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace x360n::test {
namespace ucode = rex::xenos::ucode;
inline float ReducedPrecision(float value) {
  if (!std::isfinite(value) || value == 0) return value;
  int exponent;
  const double scaled = std::ldexp(std::frexp(std::fabs(double(value)), &exponent), 22);
  double rounded = std::ldexp(std::round(scaled), exponent - 22);
  if (rounded > std::numeric_limits<float>::max())
    rounded = std::ldexp(std::floor(scaled), exponent - 22);
  return std::copysign(float(rounded), value);
}
inline float ScalarExpected(ucode::AluScalarOpcode opcode, float value) {
  using namespace ucode;
  double result;
  switch (opcode) {
    case AluScalarOpcode::kExp: result = std::exp2(double(value)); break;
    case AluScalarOpcode::kLog: case AluScalarOpcode::kLogc:
      result = std::log2(double(value)); break;
    case AluScalarOpcode::kRcp: case AluScalarOpcode::kRcpc: case AluScalarOpcode::kRcpf:
      result = 1.0 / double(value); break;
    case AluScalarOpcode::kRsq: case AluScalarOpcode::kRsqc: case AluScalarOpcode::kRsqf:
      result = 1.0 / std::sqrt(double(value)); break;
    case AluScalarOpcode::kSqrt: result = std::sqrt(double(value)); break;
    default: throw std::runtime_error("unexpected scalar oracle opcode");
  }
  float reduced = ReducedPrecision(float(result));
  if (opcode == AluScalarOpcode::kLogc && reduced == -INFINITY)
    return -std::numeric_limits<float>::max();
  if (std::isinf(reduced)) {
    if (opcode == AluScalarOpcode::kRcpc || opcode == AluScalarOpcode::kRsqc)
      return std::copysign(std::numeric_limits<float>::max(), reduced);
    if (opcode == AluScalarOpcode::kRcpf || opcode == AluScalarOpcode::kRsqf)
      return std::copysign(0.0f, reduced);
  }
  return reduced;
}
inline std::vector<uint32_t> Program(ucode::AluScalarOpcode op, bool address = false,
                              bool paired_mad = false) {
  using namespace ucode;
  uint64_t alloc = (uint64_t(AllocType::kPsColors) << 41) |
                   (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  uint64_t exec = 1 | (uint64_t(address ? 2 : 1) << 12) |
                  (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  std::vector<uint32_t> code{uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16),
      uint32_t(exec >> 16), (1u << 15) | (1u << 20) | (uint32_t(op) << 26), 0,
      uint32_t(paired_mad ? AluVectorOpcode::kMad : AluVectorOpcode::kMax) << 24};
  if (address) code.insert(code.end(), {(1u << 15) | (1u << 16) |
      (uint32_t(AluScalarOpcode::kRetainPrev) << 26),
      (1u << 29) | (1u << 30) | (1u << 31), uint32_t(AluVectorOpcode::kMax) << 24});
  return code;
}

}
