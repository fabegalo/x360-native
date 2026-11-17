// CPU-only: real draw utilities; expected tables are the Xenos/Canary contract.
#include <catch2/catch_test_macros.hpp>
#include <rex/xenos/register.h>
#include <rex/settings_registry.h>
#include <rex/utility.h>
#include "gpu/xenos/draw_state.h"
#include "gpu/xenos/xenos_settings.h"
#include <cmath>
#include <cstring>
#include <iostream>

using namespace rex::xenos;
namespace xenos = rex::xenos;
namespace draw_util = rex::gpu::draw_state;
template <typename T> void Set(RegisterFile& r, T v) { r[T::kRegisterIndex] = v.value; }
void Float(RegisterFile& r, uint32_t i, float v) { std::memcpy(&r[i], &v, sizeof(v)); }

TEST_CASE("Legacy MSAA settings reach the single Xenos setting owner", "[sdk][gpu][msaa-positions]") {
  struct TestSettings : rex::gpu::XenosSettings {} settings;
  auto& registry = REX_SETTINGS();
  REQUIRE(registry.Register(settings));
  rex::ScopeGuard unregister([&] { registry.Unregister(settings); });
  CHECK_FALSE(settings.sample_positions.Get());
  for (const auto* alias : {"xenos_sample_positions", "xenos.xenos_sample_positions", "xenos.sample_positions"}) {
    CAPTURE(alias);
    REQUIRE(registry.IsAlias(alias));
    REQUIRE(registry.SetText(alias, "true"));
    CHECK(settings.sample_positions.Get());
    REQUIRE(registry.SetText("gpu.xenos.sample_positions", "false"));
    CHECK_FALSE(settings.sample_positions.Get());
  }
  CHECK_FALSE(registry.SetText("gpu.xenos.sample_positions", "invalid"));
  CHECK_FALSE(settings.sample_positions.Get());
}

TEST_CASE("Xenos MSAA positions preserve host mapping and viewport sample fractions", "[sdk][gpu][msaa-positions]") {
  struct TestSettings : rex::gpu::XenosSettings {} settings;
  auto check = [](bool ok, const char* name) {
    INFO(name);
    CHECK(ok);
  };
  constexpr int8_t guest2[2][2] = {{-4, 4}, {4, -4}};
  constexpr int8_t guest4[4][2] = {{-2, -2}, {2, 2}, {-6, 6}, {6, -6}};
  constexpr unsigned interlock4[4] = {0, 3, 2, 1};
  for (bool native : {false, true}) for (bool interlock : {false, true}) {
    for (auto mode : {xenos::MSAASamples::k2X, xenos::MSAASamples::k4X}) {
      int8_t actual[4][2]{};
      auto count = draw_util::GetHostSampleXenosPositions(mode, native, interlock, actual);
      const bool four = mode == xenos::MSAASamples::k4X;
      check(count == (four || !native ? 4u : 2u), "native/emulated host count");
      for (unsigned h = 0; h < count; ++h) {
        const unsigned g = four ? (interlock ? interlock4[h] : h)
                               : (native ? h ^ 1u : h & 1u);
        const auto* expected = four ? guest4[g] : guest2[g];
        check(actual[h][0] == expected[0] && actual[h][1] == expected[1],
              "Xenos sample position and host/guest mapping");
      }
    }
  }
  for (auto mode : {xenos::MSAASamples::k1X, xenos::MSAASamples::k2X, xenos::MSAASamples::k4X}) {
    RegisterFile regs;
    std::memset(regs.values, 0, sizeof(regs.values));
    reg::RB_SURFACE_INFO surface{}; surface.msaa_samples = mode; Set(regs, surface);
    reg::PA_CL_VTE_CNTL vte{};
    vte.vport_x_scale_ena = vte.vport_x_offset_ena = 1;
    vte.vport_y_scale_ena = vte.vport_y_offset_ena = 1; Set(regs, vte);
    reg::PA_SU_VTX_CNTL center{}; center.pix_center = xenos::PixelCenter::kD3DZero; Set(regs, center);
    const float dx = mode == xenos::MSAASamples::k2X ? -0.25f : -0.125f;
    const float dy = mode == xenos::MSAASamples::k2X ? 0.25f : -0.125f;
    Float(regs, std::to_underlying(Register::PA_CL_VPORT_XSCALE), 640.f);
    Float(regs, std::to_underlying(Register::PA_CL_VPORT_YSCALE), -360.f);
    Float(regs, std::to_underlying(Register::PA_CL_VPORT_XOFFSET), 640.f + dx);
    Float(regs, std::to_underlying(Register::PA_CL_VPORT_YOFFSET), 360.f + dy);
    for (unsigned scale : {1u, 2u, 4u}) {
      draw_util::HostViewport standard{}, xenos{};
      draw_util::GetHostViewport(settings, regs, scale, scale, false, 16384, 16384, false,
          reg::RB_DEPTHCONTROL{}, false, false, false, standard, false);
      draw_util::GetHostViewport(settings, regs, scale, scale, false, 16384, 16384, false,
          reg::RB_DEPTHCONTROL{}, false, false, false, xenos, true);
      auto center_host = [](const draw_util::HostViewport& v, unsigned axis) {
        return double(v.xy_offset[axis]) + double(v.xy_extent[axis]) *
            (0.5 + double(v.ndc_offset[axis]) * 0.5);
      };
      const bool adjust = mode != xenos::MSAASamples::k1X && scale > 1;
      check(std::abs(center_host(xenos, 0) - center_host(standard, 0) -
                     (adjust ? -dx * (scale - 1) : 0.f)) < 0.001,
            "viewport X scales whole pixels, preserves sample fraction; 1x unchanged");
      check(std::abs(center_host(xenos, 1) - center_host(standard, 1) -
                     (adjust ? -dy * (scale - 1) : 0.f)) < 0.001,
            "viewport Y scales whole pixels, preserves sample fraction; 1x unchanged");
    }
  }
}
