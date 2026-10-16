#include <cstring>
#include <iostream>
#include <rex/graphics/register_file.h>
#include <rex/graphics/util/draw.h>
#include <rex/system/xmemory.h>

using namespace rex::graphics;
template <typename T> void Set(RegisterFile &regs, T value) {
  regs[T::register_index] = value.value;
}

int main() {
  rex::memory::Memory memory;
  if (!memory.Initialize())
    return 2;
  constexpr uint32_t address = 0x01000000;
  if (!memory.LookupHeap(0xA1000000)
           ->AllocFixed(0xA1000000, 4096, 4096,
                        rex::memory::kMemoryAllocationReserve |
                            rex::memory::kMemoryAllocationCommit,
                        rex::memory::kMemoryProtectRead |
                            rex::memory::kMemoryProtectWrite))
    return 3;
  int failures = 0, checks = 0;
  auto check = [&](bool ok, const char *name) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << "FAIL " << name << '\n';
    }
  };
  for (auto pixel_center :
       {xenos::PixelCenter::kD3DZero, xenos::PixelCenter::kOGLHalf}) {
    for (uint32_t scale : {1u, 2u}) {
      for (uint32_t sample : {0u, 1u, 2u}) {
        for (int mode = 0; mode < 8; ++mode) {
          RegisterFile regs;
          std::memset(regs.values, 0, sizeof(regs.values));
          xenos::xe_gpu_vertex_fetch_t fetch{};
          fetch.type = xenos::FetchConstantType::kVertex;
          fetch.address = address / 4;
          fetch.size = 6;
          std::memcpy(&regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], &fetch,
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
          surface.msaa_samples = xenos::MsaaSamples(sample);
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
          draw_util::ResolveInfo info;
          std::memset(&info, 0xCD, sizeof(info));
          const bool ok = draw_util::GetResolveInfo(regs, memory, scale, scale,
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
  return failures ? 1 : 0;
}
