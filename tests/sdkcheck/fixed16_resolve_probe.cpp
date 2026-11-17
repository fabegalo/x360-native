// patch 0087: fixed-point k_16_16[_16_16] render targets are s5.10 on the guest
// (stored integer = value * 1024). Motocross Madness packs DXT1 blocks as raw
// 16-bit integers through such a target and resolves them as signed integers
// with exponent bias 10; one LSB off corrupts the block.
#include <cmath>
#include <cstring>
#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include <rex/xenos/register.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/title_runtime.h>
#include "gpu/xenos/draw_state.h"
#include "gpu/xenos/xenos_settings.h"
#include "kernel/title_context.h"
#include "kernel/title_state.h"
#include "kernel/kernel_fixture.h"
using namespace rex::xenos;
namespace xenos = rex::xenos;
namespace draw_util = rex::gpu::draw_state;
template <typename T> void Set(RegisterFile &regs, T value) {
  regs[T::kRegisterIndex] = value.value;
}

// Direct3D float -> SNORM16 conversion of the host render target.
static int32_t Snorm16(float value) {
  value = std::fmax(-1.0f, std::fmin(1.0f, value));
  return int32_t(std::lround(value * 32767.0f));
}

TEST_CASE("Fixed16 resolves preserve every s5.10 integer", "[sdk][gpu][resolve-dest]") {
  auto check = [](bool ok, const char* description) {
    INFO(description);
    CHECK(ok);
  };

  // Exponent bias -10 (integer outputs, as the title writes) and 0 (outputs in
  // -32...32): every representable s5.10 integer must survive SNORM16 storage.
  for (auto format : {xenos::ColorRenderTargetFormat::k_16_16,
                      xenos::ColorRenderTargetFormat::k_16_16_16_16}) {
    for (int32_t bias : {-10, 0}) {
      const float scale = draw_util::GetColorExpBiasScale(format, bias, true);
      uint32_t mismatches = 0;
      for (int32_t n = -32767; n <= 32767; ++n) {
        const float output = bias ? float(n) : float(n) / 1024.0f;
        if (Snorm16(output * scale) != n) ++mismatches;
      }
      check(mismatches == 0, "full-range SNORM16 stores the exact s5.10 integer");
    }
  }
  // The pre-patch division by 32 alone loses the low bit for large values.
  {
    uint32_t mismatches = 0;
    for (int32_t n = -32767; n <= 32767; ++n)
      if (Snorm16(float(n) * std::ldexp(1.0f, -15)) != n) ++mismatches;
    std::cout << "division by 32 alone: " << mismatches << " of 65535 integers change\n";
    check(mismatches != 0, "dividing by 32 alone is not exact (premise)");
  }
  // Other formats, ROV/truncated storage: plain 2^bias, unchanged.
  check(draw_util::GetColorExpBiasScale(xenos::ColorRenderTargetFormat::k_8_8_8_8, -3, true) ==
            0.125f,
        "non-fixed16 formats keep 2^bias");
  check(draw_util::GetColorExpBiasScale(xenos::ColorRenderTargetFormat::k_16_16_16_16, 2, false) ==
            4.0f,
        "fixed16 without full-range SNORM keeps 2^bias");

  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
  auto& memory = *rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
  rex::kernel::TitleContext physical(caller.GetRuntime());
  struct TestSettings : rex::gpu::XenosSettings {} settings;
  constexpr uint32_t address = 0x01000000;
  REQUIRE(memory.LookupHeap(0xA1000000)->AllocFixed(0xA1000000,4096,4096,
      rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
      rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite));

  float vertices[] = {0, 0, 16, 0, 0, 16};
  std::memcpy(memory.TranslatePhysical(address), vertices, sizeof(vertices));

  struct Case {
    xenos::ColorRenderTargetFormat source;
    xenos::ColorFormat dest;
    xenos::SurfaceNumberFormat number;
    int32_t dest_bias;
    uint32_t sample_select;
    bool storage;
    draw_util::ResolveCopyShaderIndex expected;
    const char *name;
  };
  using S = draw_util::ResolveCopyShaderIndex;
  using N = xenos::SurfaceNumberFormat;
  const Case cases[] = {
      {xenos::ColorRenderTargetFormat::k_16_16_16_16, xenos::ColorFormat::k_16_16_16_16,
       N::kSignedInteger, 10, 0, true, S::kFast64bpp1x2xMSAA, "64bpp signed integer bias 10 is raw"},
      {xenos::ColorRenderTargetFormat::k_16_16, xenos::ColorFormat::k_16_16, N::kSignedInteger, 10,
       0, true, S::kFast32bpp1x2xMSAA, "32bpp signed integer bias 10 is raw"},
      {xenos::ColorRenderTargetFormat::k_16_16_16_16, xenos::ColorFormat::k_16_16_16_16,
       N::kSignedInteger, 10, 0, false, S::kFull64bpp, "inexact storage keeps the full path"},
      {xenos::ColorRenderTargetFormat::k_16_16_16_16, xenos::ColorFormat::k_16_16_16_16,
       N::kSignedInteger, 9, 0, true, S::kFull64bpp, "other bias is not raw"},
      {xenos::ColorRenderTargetFormat::k_16_16_16_16, xenos::ColorFormat::k_16_16_16_16,
       N::kSignedRepeatingFraction, 10, 0, true, S::kFull64bpp, "fractions are not raw"},
      {xenos::ColorRenderTargetFormat::k_16_16_16_16, xenos::ColorFormat::k_16_16_16_16,
       N::kSignedInteger, 10, 4, true, S::kFull64bpp, "sample averaging is not raw"},
      {xenos::ColorRenderTargetFormat::k_16_16_16_16, xenos::ColorFormat::k_16_16_16_16_FLOAT,
       N::kSignedInteger, 10, 0, true, S::kFull64bpp, "other destination format is not raw"},
  };
  for (const auto &c : cases) {
    RegisterFile regs;
    std::memset(regs.values, 0, sizeof(regs.values));

    xenos::VertexFetch fetch{};
    fetch.type = xenos::FetchConstantType::kVertex;
    fetch.address = address / 4;
    fetch.size = 6;
    std::memcpy(&regs[std::to_underlying(xenos::Register::SHADER_CONSTANT_FETCH_00_0)], &fetch, sizeof(fetch));
    reg::RB_SURFACE_INFO surface{};
    surface.surface_pitch = 128;
    // Averaging needs more than one sample; 1x selections collapse to sample 0.
    surface.msaa_samples = c.sample_select >= 4 ? xenos::MSAASamples::k2X : xenos::MSAASamples::k1X;
    Set(regs, surface);
    reg::PA_SC_WINDOW_SCISSOR_TL tl{};
    tl.window_offset_disable = 1;
    Set(regs, tl);
    reg::PA_SC_WINDOW_SCISSOR_BR br{};
    br.br_x = br.br_y = 128;
    Set(regs, br);
    reg::PA_SC_SCREEN_SCISSOR_BR screen{};
    screen.br_x = screen.br_y = 128;
    Set(regs, screen);
    reg::RB_COLOR_INFO color{};
    color.color_format = c.source;
    color.color_exp_bias = -10;
    regs[reg::RB_COLOR_INFO::kRTRegisterIndices[0]] = color.value;
    reg::RB_COPY_DEST_INFO dest{};
    dest.copy_dest_format = c.dest;
    dest.copy_dest_number = c.number;
    dest.copy_dest_exp_bias = c.dest_bias;
    dest.copy_dest_endian = xenos::Endian128::k8in16;
    Set(regs, dest);
    reg::RB_COPY_DEST_PITCH pitch{};
    pitch.copy_dest_pitch = pitch.copy_dest_height = 128;
    Set(regs, pitch);
    reg::RB_COPY_CONTROL control{};
    control.copy_command = xenos::CopyCommand::kRaw;
    control.copy_sample_select = xenos::CopySampleSelect(c.sample_select);
    Set(regs, control);
    draw_util::ResolveRequest info;
    std::memset(&info, 0xCD, sizeof(info));
    const bool ok = draw_util::GetResolveInfo(settings, regs, physical, 1, 1, false, false, info, c.storage);
    check(ok && info.coordinate_info.width_div_8 && info.height_div_8, "resolve is valid");
    draw_util::ResolveCopyShaderConstants constants;
    uint32_t groups_x = 0, groups_y = 0;
    check(info.GetCopyShader(1, 1, constants, groups_x, groups_y) == c.expected, c.name);
  }
  });
}
