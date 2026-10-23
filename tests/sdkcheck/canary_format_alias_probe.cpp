// patch 0184: k_8_A / k_8_B / k_8_8_8_8_A are aliases of k_8 / k_8_8_8_8 for
// textures and resolve destinations (Xenia Canary 8322658). No GPU.
#include <cstring>
#include <iostream>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/util/draw.h>
#include <rex/system/xmemory.h>

using namespace rex::graphics;
template <typename T> void Set(RegisterFile &regs, T value) {
  regs[T::register_index] = value.value;
}

int main() {
  int failures = 0, checks = 0;
  auto check = [&](bool ok, const char *name) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << "FAIL " << name << '\n';
    }
  };

  check(GetBaseFormat(xenos::TextureFormat::k_8_A) == xenos::TextureFormat::k_8,
        "k_8_A texture is k_8");
  check(GetBaseFormat(xenos::TextureFormat::k_8_B) == xenos::TextureFormat::k_8,
        "k_8_B texture is k_8");
  check(GetBaseFormat(xenos::TextureFormat::k_8_8_8_8_A) ==
            xenos::TextureFormat::k_8_8_8_8,
        "k_8_8_8_8_A texture is k_8_8_8_8");
  check(GetBaseFormat(xenos::TextureFormat::k_8) == xenos::TextureFormat::k_8 &&
            GetBaseFormat(xenos::TextureFormat::k_8_8_8_8_AS_16_16_16_16) ==
                xenos::TextureFormat::k_8_8_8_8,
        "other base formats unchanged");

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
  const struct {
    xenos::ColorFormat guest, expected;
  } cases[] = {
      {xenos::ColorFormat::k_8_A, xenos::ColorFormat::k_8},
      {xenos::ColorFormat::k_8_B, xenos::ColorFormat::k_8},
      {xenos::ColorFormat::k_8_8_8_8_A, xenos::ColorFormat::k_8_8_8_8},
      {xenos::ColorFormat::k_8_8_8_8, xenos::ColorFormat::k_8_8_8_8},
  };
  for (const auto &c : cases) {
    RegisterFile regs;
    std::memset(regs.values, 0, sizeof(regs.values));
    xenos::xe_gpu_vertex_fetch_t fetch{};
    fetch.type = xenos::FetchConstantType::kVertex;
    fetch.address = address / 4;
    fetch.size = 6;
    std::memcpy(&regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], &fetch,
                sizeof(fetch));
    float vertices[] = {0, 0, 16, 0, 0, 16};
    std::memcpy(memory.TranslatePhysical(address), vertices, sizeof(vertices));
    reg::RB_SURFACE_INFO surface{};
    surface.surface_pitch = 128;
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
    reg::PA_SU_VTX_CNTL center{};
    center.pix_center = xenos::PixelCenter::kOGLHalf;
    Set(regs, center);
    reg::RB_COPY_DEST_INFO dest{};
    dest.copy_dest_format = c.guest;
    Set(regs, dest);
    reg::RB_COPY_DEST_PITCH pitch{};
    pitch.copy_dest_pitch = pitch.copy_dest_height = 128;
    Set(regs, pitch);
    reg::RB_COPY_CONTROL control{};
    control.copy_command = xenos::CopyCommand::kRaw;
    Set(regs, control);
    draw_util::ResolveInfo info;
    std::memset(&info, 0xCD, sizeof(info));
    check(draw_util::GetResolveInfo(regs, memory, 1, 1, false, false, info),
          "resolve succeeds");
    check(info.copy_dest_info.copy_dest_format == c.expected,
          "resolve destination uses the aliased format");
    check(info.copy_dest_extent_length != 0, "resolve destination is writable");
  }
  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
