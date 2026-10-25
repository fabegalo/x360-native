// CPU-only: real draw utilities; expected tables are the Xenos/Canary contract.
#include <rex/graphics/register_file.h>
#include <rex/graphics/util/draw.h>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace rex::graphics;
template <typename T> void Set(RegisterFile& r, T v) { r[T::register_index] = v.value; }
void Float(RegisterFile& r, uint32_t i, float v) { std::memcpy(&r[i], &v, sizeof(v)); }

int main() {
  int checks = 0, failures = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  constexpr int8_t guest2[2][2] = {{-4, 4}, {4, -4}};
  constexpr int8_t guest4[4][2] = {{-2, -2}, {2, 2}, {-6, 6}, {6, -6}};
  constexpr unsigned interlock4[4] = {0, 3, 2, 1};
  for (bool native : {false, true}) for (bool interlock : {false, true}) {
    for (auto mode : {xenos::MsaaSamples::k2X, xenos::MsaaSamples::k4X}) {
      int8_t actual[4][2]{};
      auto count = draw_util::GetHostSampleXenosPositions(mode, native, interlock, actual);
      const bool four = mode == xenos::MsaaSamples::k4X;
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
  for (auto mode : {xenos::MsaaSamples::k1X, xenos::MsaaSamples::k2X, xenos::MsaaSamples::k4X}) {
    RegisterFile regs;
    std::memset(regs.values, 0, sizeof(regs.values));
    reg::RB_SURFACE_INFO surface{}; surface.msaa_samples = mode; Set(regs, surface);
    reg::PA_CL_VTE_CNTL vte{};
    vte.vport_x_scale_ena = vte.vport_x_offset_ena = 1;
    vte.vport_y_scale_ena = vte.vport_y_offset_ena = 1; Set(regs, vte);
    reg::PA_SU_VTX_CNTL center{}; center.pix_center = xenos::PixelCenter::kD3DZero; Set(regs, center);
    const float dx = mode == xenos::MsaaSamples::k2X ? -0.25f : -0.125f;
    const float dy = mode == xenos::MsaaSamples::k2X ? 0.25f : -0.125f;
    Float(regs, XE_GPU_REG_PA_CL_VPORT_XSCALE, 640.f);
    Float(regs, XE_GPU_REG_PA_CL_VPORT_YSCALE, -360.f);
    Float(regs, XE_GPU_REG_PA_CL_VPORT_XOFFSET, 640.f + dx);
    Float(regs, XE_GPU_REG_PA_CL_VPORT_YOFFSET, 360.f + dy);
    for (unsigned scale : {1u, 2u, 4u}) {
      draw_util::ViewportInfo standard{}, xenos{};
      draw_util::GetHostViewportInfo(regs, scale, scale, false, 16384, 16384, false,
          reg::RB_DEPTHCONTROL{}, false, false, false, standard, false);
      draw_util::GetHostViewportInfo(regs, scale, scale, false, 16384, 16384, false,
          reg::RB_DEPTHCONTROL{}, false, false, false, xenos, true);
      auto center_host = [](const draw_util::ViewportInfo& v, unsigned axis) {
        return double(v.xy_offset[axis]) + double(v.xy_extent[axis]) *
            (0.5 + double(v.ndc_offset[axis]) * 0.5);
      };
      const bool adjust = mode != xenos::MsaaSamples::k1X && scale > 1;
      check(std::abs(center_host(xenos, 0) - center_host(standard, 0) -
                     (adjust ? -dx * (scale - 1) : 0.f)) < 0.001,
            "viewport X scales whole pixels, preserves sample fraction; 1x unchanged");
      check(std::abs(center_host(xenos, 1) - center_host(standard, 1) -
                     (adjust ? -dy * (scale - 1) : 0.f)) < 0.001,
            "viewport Y scales whole pixels, preserves sample fraction; 1x unchanged");
    }
  }
  std::cout << "MSAA CPU positions/viewport: " << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
