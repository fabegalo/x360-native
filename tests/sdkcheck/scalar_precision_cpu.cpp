// Exercise the interpreter used by draw extent estimation with guest ALU words.
#include "scalar_precision_oracle.h"
#include <rex/graphics/pipeline/shader/interpreter.h>
#include <rex/graphics/registers.h>
#include <rex/cvar.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <iostream>
#include <vector>
using namespace rex::graphics;
namespace {
struct Sink : ShaderInterpreter::ExportSink {
  float value = 0;
  unsigned writes = 0;
  void Export(ucode::ExportRegister, const float* v, uint32_t mask) override {
    if (mask & 1) { value = v[0]; ++writes; }
  }
};
std::vector<uint32_t> Program(ucode::AluScalarOpcode op, bool address = false,
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
int main() {
  RegisterFile regs;
  std::memset(regs.values, 0, sizeof(regs.values));
  regs[XE_GPU_REG_SQ_PS_CONST] = 0x000FF100;
  rex::memory::Memory memory;
  ShaderInterpreter interpreter(regs, memory);
  std::fill_n(interpreter.temp_registers(), xenos::kMaxShaderTempRegisters * 4, 0.0f);
  Sink sink;
  interpreter.SetExportSink(&sink);
  auto constant = [&](unsigned index, unsigned component, float value) {
    regs[XE_GPU_REG_SHADER_CONSTANT_000_X + (256 + index) * 4 + component] =
        std::bit_cast<uint32_t>(value);
  };
  auto execute = [&](const std::vector<uint32_t>& code) {
    sink.writes = 0;
    interpreter.SetShader(xenos::ShaderType::kPixel, code.data());
    interpreter.Execute();
    return sink.value;
  };
  unsigned checks = 0, failures = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  using O = ucode::AluScalarOpcode;
  for (auto op : {O::kExp, O::kLog, O::kLogc, O::kRcp, O::kRcpc, O::kRcpf,
                  O::kRsq, O::kRsqc, O::kRsqf, O::kSqrt}) {
    auto code = Program(op);
    for (float input : {0.0f, -0.0f, .125f, .75f, 1.0f, 2.0f, 3.0f, 16.0f,
                        -2.0f, INFINITY, -INFINITY, NAN}) {
      constant(0, 3, input);
      float expected = x360n::test::ScalarExpected(op, input), got = execute(code);
      check(sink.writes == 1 && (std::isnan(expected) ? std::isnan(got) :
          std::bit_cast<uint32_t>(expected) == std::bit_cast<uint32_t>(got)),
          "reduced scalar precision and special values");
    }
  }
  for (unsigned i = 0; i != 256; ++i) constant(i, 0, 1000.0f + i);
  for (auto op : {O::kMaxAs, O::kMaxAsf}) {
    auto code = Program(op, true);
    for (float input : {-256.5f, -1.25f, -.5f, 0.0f, .4f, .5f, 254.5f,
                        255.0f, 256.0f, 500.0f}) {
      constant(0, 3, input);
      int index = std::clamp(int(std::floor(input + (op == O::kMaxAs ? .5f : 0))), 0, 255);
      check(execute(code) == 1000.0f + index && sink.writes == 2,
            "scalar a0 clamps and addresses the expected constant");
    }
  }
  constant(0, 0, .25f); constant(0, 2, .75f); constant(0, 3, 2.0f);
  check(execute(Program(O::kMuls, false, true)) == .5f && sink.writes == 1,
        "paired MAD scalar operand remains WX like the shared GPU parser");
  auto mulsc = Program(O::kMulsc0);
  for (bool rtz : {false, true}) {
    check(rex::cvar::SetFlagByName("mulsc_round_toward_zero", rtz ? "true" : "false"),
          "explicit mulsc rounding setting");
    for (float a : {.1f, -.1f, .3f, 1.3f}) {
      for (float b : {.1f, .3f, -1.1f, 9.876543f}) {
        constant(0, 3, a); interpreter.temp_registers()[0] = b;
        double exact = double(a) * double(b);
        float expected = float(exact);
        if (rtz && std::fabs(double(expected)) > std::fabs(exact))
          expected = std::nextafter(expected, 0.0f);
        check(std::bit_cast<uint32_t>(execute(mulsc)) == std::bit_cast<uint32_t>(expected),
              "mulsc default and optional rounding");
      }
    }
  }
  std::cout << checks - failures << '/' << checks << " scalar interpreter checks passed\n";
  return failures ? 1 : 0;
}
