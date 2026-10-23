// patch 0191: a host viewport may start at a negative coordinate, so tiles
// drawn with negative window offsets keep the NDC transform of the others
// (Xenia Canary 6579cc2a4). No GPU.
#include <cstring>
#include <iostream>
#include <rex/graphics/register_file.h>
#include <rex/graphics/util/draw.h>

using namespace rex::graphics;
template <typename T> void Set(RegisterFile &regs, T value) {
  regs[T::register_index] = value.value;
}

namespace {
void SetFloat(RegisterFile &regs, uint32_t index, float value) {
  std::memcpy(&regs.values[index], &value, sizeof(value));
}
draw_util::ViewportInfo Viewport(int32_t window_x_offset) {
  RegisterFile regs;
  std::memset(regs.values, 0, sizeof(regs.values));
  reg::PA_CL_VTE_CNTL vte{};
  vte.vport_x_scale_ena = vte.vport_x_offset_ena = 1;
  vte.vport_y_scale_ena = vte.vport_y_offset_ena = 1;
  Set(regs, vte);
  SetFloat(regs, XE_GPU_REG_PA_CL_VPORT_XSCALE, 640.0f);
  SetFloat(regs, XE_GPU_REG_PA_CL_VPORT_XOFFSET, 640.0f);
  SetFloat(regs, XE_GPU_REG_PA_CL_VPORT_YSCALE, -360.0f);
  SetFloat(regs, XE_GPU_REG_PA_CL_VPORT_YOFFSET, 360.0f);
  reg::PA_SU_SC_MODE_CNTL mode{};
  mode.vtx_window_offset_enable = 1;
  Set(regs, mode);
  reg::PA_SC_WINDOW_OFFSET window{};
  window.window_x_offset = window_x_offset;
  Set(regs, window);
  reg::PA_SU_VTX_CNTL center{};
  center.pix_center = xenos::PixelCenter::kOGLHalf;
  Set(regs, center);
  draw_util::ViewportInfo info;
  draw_util::GetHostViewportInfo(regs, 1, 1, false, 8192, 8192, false, reg::RB_DEPTHCONTROL{},
                                 false, false, false, info);
  return info;
}
}  // namespace

int main() {
  int failures = 0, checks = 0;
  auto check = [&](bool ok, const char *name) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << "FAIL " << name << '\n';
    }
  };
  const auto left = Viewport(0);
  const auto right = Viewport(-608);  // 4D5307DF's right-hand tile.
  check(left.xy_offset[0] == 0 && left.xy_extent[0] == 1280, "left tile viewport");
  check(right.xy_offset[0] == -608, "right tile viewport starts at -608");
  check(right.xy_extent[0] == 1280, "right tile viewport keeps the full extent");
  check(right.ndc_scale[0] == left.ndc_scale[0] && right.ndc_offset[0] == left.ndc_offset[0],
        "both tiles use the same NDC transform");
  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
