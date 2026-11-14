#pragma once
#include <rex/graphics/format/ucode.h>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace x360n::test {
namespace ucode = rex::graphics::ucode;
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
}
