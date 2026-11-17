#include <cstring>
#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include <rex/xenos/register.h>
#include "gpu/xenos/draw_state.h"
#include "gpu/xenos/xenos_settings.h"
#include <rex/kernel/heap.h>
#include "kernel/title_context.h"
#include "kernel/title_state.h"
#include "kernel/kernel_fixture.h"

using namespace rex::xenos;
namespace xenos = rex::xenos;
namespace reg = rex::xenos::reg;
namespace draw_util = rex::gpu::draw_state;
template <typename T> void Set(RegisterFile &regs, T value) {
  regs[T::kRegisterIndex] = value.value;
}

TEST_CASE("Empty resolves preserve zero copy and clear extents before alignment", "[sdk][gpu][resolve-dest]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
  auto& memory = *rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
  rex::kernel::TitleContext physical(caller.GetRuntime());
  struct TestSettings : rex::gpu::XenosSettings {} settings;
  constexpr uint32_t address = 0x01000000;
  REQUIRE(memory.LookupHeap(0xA1000000)->AllocFixed(0xA1000000,4096,4096,
      rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
      rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite));
  int failures = 0, checks = 0;
  auto check = [&](bool ok, const char *name) {
    ++checks;
    INFO(name);
    CHECK(ok);
  };
  for (auto pixel_center :
       {xenos::PixelCenter::kD3DZero, xenos::PixelCenter::kOGLHalf}) {
    for (uint32_t scale : {1u, 2u}) {
      for (uint32_t sample : {0u, 1u, 2u}) {
        for (int mode = 0; mode < 8; ++mode) {
          RegisterFile regs;
          std::memset(regs.values, 0, sizeof(regs.values));
          xenos::VertexFetch fetch{};
          fetch.type = xenos::FetchConstantType::kVertex;
          fetch.address = address / 4;
          fetch.size = 6;
          std::memcpy(&regs[std::to_underlying(xenos::Register::SHADER_CONSTANT_FETCH_00_0)], &fetch,
                      sizeof(fetch));
          float vertices[] = {0, 0, 16, 0, 0, 16};
          // Degenerate aligned / unaligned axes, subpixel coverage, scissor
          // exclusion, and pitch clipping must all remain empty after
          // alignment.
          if (mode == 0)
            vertices[2] = 0;
          if (mode == 1)
            vertices[0] = vertices[2] = vertices[4] = 3;
          if (mode == 2)
            vertices[1] = vertices[3] = vertices[5] = 5;
          if (mode == 3)
            vertices[2] = 0.1f;
          if (pixel_center == xenos::PixelCenter::kD3DZero) {
            for (float &coordinate : vertices)
              coordinate -= 0.5f;
          }
          std::memcpy(memory.TranslatePhysical(address), vertices,
                      sizeof(vertices));
          reg::RB_SURFACE_INFO surface{};
          surface.surface_pitch = mode == 6 ? 0 : 128;
          surface.msaa_samples = xenos::MSAASamples(sample);
          Set(regs, surface);
          reg::PA_SC_WINDOW_SCISSOR_TL tl{};
          tl.window_offset_disable = 1;
          tl.tl_x = mode == 4 ? 19 : 0;
          tl.tl_y = mode == 5 ? 21 : 0;
          Set(regs, tl);
          reg::PA_SC_WINDOW_SCISSOR_BR br{};
          br.br_x = br.br_y = 128;
          Set(regs, br);
          reg::PA_SC_SCREEN_SCISSOR_BR screen{};
          screen.br_x = screen.br_y = 128;
          Set(regs, screen);
          reg::PA_SU_VTX_CNTL center{};
          center.pix_center = pixel_center;
          Set(regs, center);
          reg::RB_COPY_DEST_INFO dest{};
          dest.copy_dest_format = xenos::ColorFormat::k_8_8_8_8;
          Set(regs, dest);
          reg::RB_COPY_DEST_PITCH pitch{};
          pitch.copy_dest_pitch = pitch.copy_dest_height = 128;
          Set(regs, pitch);
          reg::RB_COPY_CONTROL control{};
          control.copy_command = xenos::CopyCommand::kRaw;
          control.color_clear_enable = control.depth_clear_enable = 1;
          Set(regs, control);
          draw_util::ResolveRequest info;
          std::memset(&info, 0xCD, sizeof(info));
          const bool ok = draw_util::GetResolveInfo(settings, regs, physical, scale, scale,
                                                    false, false, info);
          check(ok, "valid resolve succeeds");
          if (mode == 7) {
            check(info.coordinate_info.width_div_8 == 2 &&
                      info.height_div_8 == 2,
                  "nonempty resolve preserves extent");
            check(info.copy_dest_extent_length != 0,
                  "nonempty destination remains writable");
          } else {
            check(info.coordinate_info.width_div_8 == 0 &&
                      info.height_div_8 == 0,
                  "empty resolve cannot copy or clear");
          }
        }
      }
    }
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  REQUIRE(failures == 0);
  });
}
