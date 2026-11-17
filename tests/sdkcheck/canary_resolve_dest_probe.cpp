// patch 0187: D3D advances RB_COPY_DEST_BASE by 32x32 macro tiles, which are
// smaller than 4 KB below 32bpp; the remainder is an X offset (Xenia Canary
// 89609297c). patch 0192: volume resolves take the slice height from
// RB_COPY_SURFACE_SLICE (d8731edc9). No GPU.
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

namespace {
constexpr uint32_t kVertices = 0x01000000;

bool Resolve(rex::kernel::GuestAddressSpace& memory, rex::IPhysicalMemory& physical, const rex::gpu::XenosSettings& settings, uint32_t dest_base, float x0,
             xenos::ColorFormat format, draw_util::ResolveRequest &info, bool array = false,
             uint32_t dest_height = 256, uint32_t surface_slice = 0,
             bool gamma_source = false) {
  RegisterFile regs;
  std::memset(regs.values, 0, sizeof(regs.values));

  xenos::VertexFetch fetch{};
  fetch.type = xenos::FetchConstantType::kVertex;
  fetch.address = kVertices / 4;
  fetch.size = 6;
  std::memcpy(&regs[std::to_underlying(xenos::Register::SHADER_CONSTANT_FETCH_00_0)], &fetch,
              sizeof(fetch));
  float vertices[] = {x0, 0, x0 + 16, 0, x0, 16};
  std::memcpy(memory.TranslatePhysical(kVertices), vertices, sizeof(vertices));
  reg::RB_SURFACE_INFO surface{};
  surface.surface_pitch = 256;
  Set(regs, surface);
  reg::RB_COLOR_INFO color{};
  color.color_format = gamma_source ? xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA
                                   : xenos::ColorRenderTargetFormat::k_8_8_8_8;
  Set(regs, color);
  reg::PA_SC_WINDOW_SCISSOR_TL tl{};
  tl.window_offset_disable = 1;
  Set(regs, tl);
  reg::PA_SC_WINDOW_SCISSOR_BR br{};
  br.br_x = br.br_y = 256;
  Set(regs, br);
  reg::PA_SC_SCREEN_SCISSOR_BR screen{};
  screen.br_x = screen.br_y = 256;
  Set(regs, screen);
  reg::PA_SU_VTX_CNTL center{};
  center.pix_center = xenos::PixelCenter::kOGLHalf;
  Set(regs, center);
  reg::RB_COPY_DEST_INFO dest{};
  dest.copy_dest_format = format;
  dest.copy_dest_array = array;
  // Slices from Z = 4 on are addressed with the slice height.
  dest.copy_dest_layer = array ? 4 : 0;
  Set(regs, dest);
  reg::RB_COPY_DEST_PITCH pitch{};
  pitch.copy_dest_pitch = 256;
  pitch.copy_dest_height = dest_height;
  Set(regs, pitch);
  regs[std::to_underlying(xenos::Register::RB_COPY_SURFACE_SLICE)] = surface_slice;
  regs[std::to_underlying(xenos::Register::RB_COPY_DEST_BASE)] = dest_base;
  reg::RB_COPY_CONTROL control{};
  control.copy_command = xenos::CopyCommand::kRaw;
  Set(regs, control);
  std::memset(&info, 0xCD, sizeof(info));
  return draw_util::GetResolveInfo(settings, regs, physical, 1, 1, false, false, info);
}
}  // namespace

TEST_CASE("Resolve destinations preserve macro phase and volume slice height", "[sdk][gpu][resolve-dest]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
  auto& memory = *rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
  rex::kernel::TitleContext physical(caller.GetRuntime());
  struct TestSettings : rex::gpu::XenosSettings {} settings;
  REQUIRE(memory.LookupHeap(0xA1000000)->AllocFixed(0xA1000000,4096,4096,
      rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
      rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite));

  auto check = [](bool ok, const char* description) {
    INFO(description);
    CHECK(ok);
  };
  constexpr uint32_t base = 0x02000000;
  // A base advanced by N macro tiles is the same copy as N * 32 pixels to the
  // right of the 4 KB-aligned base.
  const struct {
    xenos::ColorFormat format;
    uint32_t macro_bytes;
    uint32_t phase;
  } cases[] = {
      {xenos::ColorFormat::k_5_6_5, 2048, 1},
      {xenos::ColorFormat::k_8, 1024, 1},
      {xenos::ColorFormat::k_8, 1024, 3},
      {xenos::ColorFormat::k_8_8_8_8, 4096, 0},
  };
  for (const auto &c : cases) {
    draw_util::ResolveRequest advanced, shifted;
    check(Resolve(memory, physical, settings, base + c.phase * c.macro_bytes, 0, c.format, advanced) &&
              Resolve(memory, physical, settings, base, float(32 * c.phase), c.format, shifted),
          "resolves succeed");
    check(advanced.copy_dest_base == shifted.copy_dest_base,
          "advanced base equals the aligned base");
    check(advanced.copy_dest_coordinate_info.offset_x_div_8 ==
                  shifted.copy_dest_coordinate_info.offset_x_div_8 &&
              advanced.copy_dest_coordinate_info.offset_y_div_8 ==
                  shifted.copy_dest_coordinate_info.offset_y_div_8,
          "base phase becomes the X offset");
    check(advanced.copy_dest_extent_start == shifted.copy_dest_extent_start &&
              advanced.copy_dest_extent_length == shifted.copy_dest_extent_length,
          "destination extents match");
  }
  // A volume destination's slice height is RB_COPY_SURFACE_SLICE / pitch, not
  // copy_dest_height (192 here, the source rectangle bottom).
  {
    draw_util::ResolveRequest slice, height, plain;
    check(Resolve(memory, physical, settings, base, 0, xenos::ColorFormat::k_8_8_8_8, slice, true, 192, 256 * 256) &&
              Resolve(memory, physical, settings, base, 0, xenos::ColorFormat::k_8_8_8_8, height, true, 256) &&
              Resolve(memory, physical, settings, base, 0, xenos::ColorFormat::k_8_8_8_8, plain, false, 192,
                      256 * 256),
          "volume resolves succeed");
    check(slice.copy_dest_coordinate_info.height_aligned_div_32 == 8,
          "volume slice height comes from RB_COPY_SURFACE_SLICE");
    check(slice.copy_dest_base == height.copy_dest_base &&
              slice.copy_dest_extent_start == height.copy_dest_extent_start &&
              slice.copy_dest_extent_length == height.copy_dest_extent_length,
          "volume destination matches the slice height");
    check(plain.copy_dest_coordinate_info.height_aligned_div_32 == 6,
          "2D destinations keep copy_dest_height");
  }
  {
    draw_util::ResolveRequest info;
    check(Resolve(memory, physical, settings, base, 0, xenos::ColorFormat::k_8_8_8_8, info, false, 256, 0, true),
          "real gamma resolve registers are accepted");
    draw_util::ResolveCopyShaderConstants constants{}; uint32_t gx=0,gy=0;
    check(info.color_edram_info.format == unsigned(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) &&
              info.GetCopyShader(1,1,constants,gx,gy,true)==draw_util::ResolveCopyShaderIndex::kFull32bpp &&
              constants.dest_relative.edram_info.format == info.color_edram_info.format,
          "GetResolveInfo carries PWL decoding through real shader selection/constants");
    check(info.GetCopyShader(1,1,constants,gx,gy,false)==draw_util::ResolveCopyShaderIndex::kFast32bpp1x2xMSAA,
          "callers without PWL opt-in preserve their selection");
  }
  });
}
