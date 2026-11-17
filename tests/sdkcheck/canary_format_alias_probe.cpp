// patch 0184: k_8_A / k_8_B / k_8_8_8_8_A are aliases of k_8 / k_8_8_8_8 for
// textures and resolve destinations (Xenia Canary 8322658). No GPU.
#include <cstring>
#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include <rex/xenos/texture.h>
#include <rex/xenos/register.h>
#include <rex/kernel/guest_memory.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/title_runtime.h>
#include "gpu/xenos/draw_state.h"
#include "gpu/xenos/xenos_settings.h"
#include "kernel/title_context.h"
#include "kernel/title_state.h"
#include "kernel/kernel_fixture.h"

using namespace rex::xenos;
namespace xenos = rex::xenos;
template <typename T> void Set(RegisterFile &regs, T value) {
  regs[T::kRegisterIndex] = value.value;
}

TEST_CASE("Canary texture aliases preserve resolve destinations", "[sdk][gpu][tiled-address]") {
  auto check = [](bool ok, const char *name) {
    INFO(name);
    CHECK(ok);
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

  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
  auto& memory = *rex::kernel::StateOf(caller.GetRuntime()).GetAddressSpace();
  rex::kernel::TitleContext physical(caller.GetRuntime());
  struct TestSettings : rex::gpu::XenosSettings {} settings;
  constexpr uint32_t address = 0x01000000;
  REQUIRE(memory.LookupHeap(0xA1000000)->AllocFixed(
      0xA1000000, 4096, 4096,
      rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
      rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite));
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

    xenos::VertexFetch fetch{};
    fetch.type = xenos::FetchConstantType::kVertex;
    fetch.address = address / 4;
    fetch.size = 6;
    std::memcpy(&regs[std::to_underlying(xenos::Register::SHADER_CONSTANT_FETCH_00_0)], &fetch,
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
    rex::gpu::draw_state::ResolveRequest info;
    std::memset(&info, 0xCD, sizeof(info));
    check(rex::gpu::draw_state::GetResolveInfo(settings, regs, physical, 1, 1, false, false, info),
          "resolve succeeds");
    check(info.copy_dest_info.copy_dest_format == c.expected,
          "resolve destination uses the aliased format");
    check(info.copy_dest_extent_length != 0, "resolve destination is writable");
  }
  });
}
