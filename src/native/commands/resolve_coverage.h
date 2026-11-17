// Coverage calculation follows ReXGlue f5337cdc graphics/util/draw.cpp,
// Copyright 2023 Ben Vanik, BSD license (external/rexglue-sdk/LICENSE).
#pragma once
#include <gpu/xenos/draw_state.h>
#include <rex/xenos/register.h>
#include <gpu/xenos/host/fixed_point.h>
#include <algorithm>
#include <cmath>
namespace x360n::native {
namespace draw_util = rex::gpu::draw_state;
// Only an empty, otherwise supported resolve is a no-op. Malformed inputs
// must continue into GetResolveInfo's diagnostic failure path.
inline bool ResolveCoverageEmpty(const rex::xenos::RegisterFile& r, const float* vertices) {
  using namespace rex::gpu;
using namespace rex::xenos;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
  auto command = r.Get<reg::RB_COPY_CONTROL>().copy_command;
  if (command != xenos::CopyCommand::kRaw && command != xenos::CopyCommand::kConvert) return false;
  const auto surface = r.Get<reg::RB_SURFACE_INFO>();
  if (surface.msaa_samples > xenos::MSAASamples::k4X) return false;
  const float half = r.Get<reg::PA_SU_VTX_CNTL>().pix_center == xenos::PixelCenter::kD3DZero ? 0.5f : 0.0f;
  int32_t fixed[6];
  for (unsigned i = 0; i < 6; ++i) {
    if (!std::isfinite(vertices[i])) return false;
    fixed[i] = rex::gpu::FloatToD3D11Fixed16p8(vertices[i] + half);
  }
  const auto offset = r.Get<reg::PA_SC_WINDOW_OFFSET>();
  const bool window = r.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable;
  draw_util::Scissor scissor;
  draw_util::GetScissor(r, scissor, false);
  for (unsigned axis = 0; axis < 2; ++axis) {
    int32_t low = (std::min({fixed[axis], fixed[axis+2], fixed[axis+4]}) + 127) >> 8;
    int32_t high = (std::max({fixed[axis], fixed[axis+2], fixed[axis+4]}) + 127) >> 8;
    if (window) {
      const int32_t shift = axis ? offset.window_y_offset : offset.window_x_offset;
      low += shift; high += shift;
    }
    const int32_t start = int32_t(scissor.offset[axis]);
    const int32_t end = int32_t(scissor.offset[axis] + scissor.extent[axis]);
    low = std::clamp(low, start, end) & ~7;
    high = (std::clamp(high, start, end) + 7) & ~7;
    if (!axis) {
      const int32_t pitch = int32_t(surface.surface_pitch & ~7u);
      low = std::min(low, pitch); high = std::min(high, pitch);
    }
    if (low >= high) return true;
  }
  return false;
}
}
