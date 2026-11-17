// patch 0191: a host viewport may start at a negative coordinate, so tiles
// drawn with negative window offsets keep the NDC transform of the others
// (Xenia Canary 6579cc2a4). No GPU.
#include <cstring>
#include <cmath>
#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include <rex/xenos/register.h>
#include "gpu/xenos/draw_state.h"
#include "gpu/xenos/xenos_settings.h"

using namespace rex::xenos;
namespace xenos = rex::xenos;
template <typename T> void Set(RegisterFile &regs, T value) {
  regs[T::kRegisterIndex] = value.value;
}

namespace {
void SetFloat(RegisterFile &regs, uint32_t index, float value) {
  std::memcpy(&regs[index], &value, sizeof(value));
}
rex::gpu::draw_state::HostViewport Viewport(int32_t window_x_offset) {
  RegisterFile regs;
  std::memset(regs.values, 0, sizeof(regs.values));
  reg::PA_CL_VTE_CNTL vte{};
  vte.vport_x_scale_ena = vte.vport_x_offset_ena = 1;
  vte.vport_y_scale_ena = vte.vport_y_offset_ena = 1;
  Set(regs, vte);
  SetFloat(regs, std::to_underlying(Register::PA_CL_VPORT_XSCALE), 640.0f);
  SetFloat(regs, std::to_underlying(Register::PA_CL_VPORT_XOFFSET), 640.0f);
  SetFloat(regs, std::to_underlying(Register::PA_CL_VPORT_YSCALE), -360.0f);
  SetFloat(regs, std::to_underlying(Register::PA_CL_VPORT_YOFFSET), 360.0f);
  reg::PA_SU_SC_MODE_CNTL mode{};
  mode.vtx_window_offset_enable = 1;
  Set(regs, mode);
  reg::PA_SC_WINDOW_OFFSET window{};
  window.window_x_offset = window_x_offset;
  Set(regs, window);
  reg::PA_SU_VTX_CNTL center{};
  center.pix_center = xenos::PixelCenter::kOGLHalf;
  Set(regs, center);
  rex::gpu::draw_state::HostViewport info;
  struct TestSettings : rex::gpu::XenosSettings {} settings;
  rex::gpu::draw_state::GetHostViewport(settings, regs, 1, 1, false, 8192, 8192, false, reg::RB_DEPTHCONTROL{},
                                 false, false, false, info);
  return info;
}
}  // namespace

TEST_CASE("Negative window offsets preserve the viewport transform", "[sdk][gpu][viewport]") {
  auto check = [](bool ok, const char *name) {
    INFO(name);
    CHECK(ok);
  };
  const auto left = Viewport(0);
  const auto right = Viewport(-608);  // 4D5307DF's right-hand tile.
  check(left.xy_offset[0] == 0 && left.xy_extent[0] == 1280, "left tile viewport");
  check(int64_t(right.xy_offset[0]) == -608, "right tile viewport starts at -608");
  check(right.xy_extent[0] == 1280, "right tile viewport keeps the full extent");
  check(right.ndc_scale[0] == left.ndc_scale[0] && right.ndc_offset[0] == left.ndc_offset[0],
        "both tiles use the same NDC transform");
}

TEST_CASE("Whole-tile EDRAM window offsets move the scissor and viewport once",
          "[sdk][gpu][viewport][edram-window]") {
  namespace draw = rex::gpu::draw_state;
  struct TestSettings : rex::gpu::XenosSettings {} settings;
  for (auto msaa : {MSAASamples::k1X, MSAASamples::k2X, MSAASamples::k4X}) {
    const int tile_width = msaa == MSAASamples::k4X ? 40 : 80;
    const int tile_height = msaa == MSAASamples::k1X ? 16 : 8;
    for (bool host : {false, true}) {
      for (bool wide : {false, true}) {
        CAPTURE(msaa, host, wide);
        RegisterFile regs;
        std::memset(regs.values, 0, sizeof(regs.values));
        reg::RB_SURFACE_INFO surface{};
        surface.surface_pitch = 1280; surface.msaa_samples = msaa; Set(regs, surface);
        reg::PA_SU_SC_MODE_CNTL mode{}; mode.vtx_window_offset_enable = 1; Set(regs, mode);
        reg::PA_SC_WINDOW_OFFSET window{}; window.window_y_offset = -tile_height; Set(regs, window);
        reg::PA_SC_WINDOW_SCISSOR_BR bounds{}; bounds.br_x = 1280; bounds.br_y = 128; Set(regs, bounds);
        reg::PA_SC_SCREEN_SCISSOR_BR screen{}; screen.br_x = screen.br_y = 8192; Set(regs, screen);
        reg::PA_CL_VTE_CNTL vte{};
        vte.vport_x_scale_ena = vte.vport_x_offset_ena = 1;
        vte.vport_y_scale_ena = vte.vport_y_offset_ena = 1; Set(regs, vte);
        SetFloat(regs, std::to_underlying(Register::PA_CL_VPORT_XSCALE), 640.f);
        SetFloat(regs, std::to_underlying(Register::PA_CL_VPORT_XOFFSET), 640.f);
        SetFloat(regs, std::to_underlying(Register::PA_CL_VPORT_YSCALE), -64.f);
        SetFloat(regs, std::to_underlying(Register::PA_CL_VPORT_YOFFSET), 64.f);
        reg::RB_COLOR_INFO color{}; color.color_base = 1024;
        color.color_format = wide ? ColorRenderTargetFormat::k_16_16_16_16 : ColorRenderTargetFormat::k_8_8_8_8;
        Set(regs, color);
        reg::RB_DEPTHCONTROL depth{}; depth.z_enable = depth.z_write_enable = 1;
        depth.stencil_enable = wide;
        bool kept = false;
        CHECK(draw::GetWindowOffsetTiles(regs, depth, 15, host, kept) == -(1280 / tile_width));
        CHECK_FALSE(kept);
        const auto color_before = regs[reg::RB_COLOR_INFO::kRegisterIndex];
        draw::Scissor original{}, shifted{};
        draw::GetScissor(regs, original, true, false);
        draw::GetScissor(regs, shifted, true, true);
        CHECK(original.extent[0] == shifted.extent[0]);
        CHECK(original.extent[1] == shifted.extent[1]);
        CHECK(shifted.offset[1] == original.offset[1] + tile_height);
        draw::HostViewport first{}, second{};
        draw::GetHostViewport(settings, regs, 1, 1, false, 16384, 16384, false, depth,
                              false, false, false, first, true, false);
        draw::GetHostViewport(settings, regs, 1, 1, false, 16384, 16384, false, depth,
                              false, false, false, second, true, true);
        const auto center = [](const draw::HostViewport& v) {
          return v.xy_offset[1] + v.xy_extent[1] * (0.5 + v.ndc_offset[1] * 0.5);
        };
        CHECK(std::abs(center(second) - center(first) - tile_height) < .001);
        CHECK(regs[reg::RB_COLOR_INFO::kRegisterIndex] == color_before);
        window.window_x_offset = -tile_width; window.window_y_offset = 0; Set(regs, window);
        CHECK(draw::GetWindowOffsetTiles(regs, depth, 15, host, kept) == (host ? 0 : -1));
        for (int y : {0, tile_height, -tile_height + 1}) {
          window.window_x_offset = 0; window.window_y_offset = y; Set(regs, window);
          CHECK(draw::GetWindowOffsetTiles(regs, depth, 15, host, kept) == 0);
        }
      }
    }
  }
  CHECK(draw::AddWindowOffsetToEdramBase(0, -1, false) == 2047);
  CHECK(draw::AddWindowOffsetToEdramBase(0, -1, true) == 2046);
}
