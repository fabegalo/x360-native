// Exercise the interpreter used by draw extent estimation with guest ALU words.
#include "scalar_precision_oracle.h"
#include <catch2/catch_test_macros.hpp>
#include "gpu/xenos/pipeline/shader/interpreter.h"
#include <rex/xenos/register.h>
#include "kernel/kernel_fixture.h"
#include "kernel/title_context.h"
#include <memory>
#include <algorithm>
#include <bit>
#include <cstring>
#include <iostream>
#include <vector>
using namespace rex::gpu;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
namespace ucode = rex::xenos::ucode;
namespace {
struct Sink : ShaderInterpreter::ExportSink {
  float value = 0;
  unsigned writes = 0;
  void Export(ucode::ExportRegister, const float* v, uint32_t mask) override {
    if (mask & 1) { value = v[0]; ++writes; }
  }
};
using x360n::test::Program;
}
TEST_CASE("Scalar interpreter preserves precision and special-value contracts", "[sdk][gpu][shader-fetch][scalar-precision]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
  RegisterFile regs;
  std::memset(regs.values, 0, sizeof(regs.values));
  regs[std::to_underlying(xenos::Register::SQ_PS_CONST)] = 0x000FF100;
  rex::kernel::TitleContext context(caller.GetRuntime());
  auto interpreter = std::make_unique<ShaderInterpreter>(regs, context);
  std::fill_n(interpreter->GetTempRegisters(), xenos::kMaxShaderTempRegisters * 4, 0.0f);
  Sink sink;
  interpreter->SetExportSink(&sink);
  auto constant = [&](unsigned index, unsigned component, float value) {
    regs[std::to_underlying(xenos::Register::SHADER_CONSTANT_000_X) + (256 + index) * 4 + component] =
        std::bit_cast<uint32_t>(value);
  };
  auto execute = [&](const std::vector<uint32_t>& code) {
    sink.writes = 0;
    interpreter->SetShader(xenos::ShaderType::kPixel, code.data());
    interpreter->Execute();
    return sink.value;
  };
  unsigned checks = 0, failures = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks;
    INFO(name);
    CHECK(ok);
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
    interpreter = std::make_unique<ShaderInterpreter>(regs, context, rtz);
    std::fill_n(interpreter->GetTempRegisters(), xenos::kMaxShaderTempRegisters * 4, 0.0f);
    interpreter->SetExportSink(&sink);
    for (float a : {.1f, -.1f, .3f, 1.3f}) {
      for (float b : {.1f, .3f, -1.1f, 9.876543f}) {
        constant(0, 3, a); interpreter->GetTempRegisters()[0] = b;
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
  CHECK(failures == 0);
  });
}
