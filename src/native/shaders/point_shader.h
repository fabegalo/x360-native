#pragma once
#include <gpu/xenos/pipeline/shader/dxbc_translator.h>
#include <rex/xenos/register.h>
#include <gpu/xenos/draw_state.h>
#include <cstdint>
#include <vector>
namespace x360n::native {
namespace draw_util = rex::gpu::draw_state;
enum class PipelineGeometryShader : uint32_t { kNone, kPointList, kRectangleList, kQuadList };
union GeometryShaderKey {
  uint32_t key = 0;
  struct {
    PipelineGeometryShader type : 2;
    uint32_t interpolator_count : 5;
    uint32_t user_clip_plane_count : 3;
    uint32_t user_clip_plane_cull : 1;
    uint32_t has_vertex_kill_and : 1;
    uint32_t has_point_size : 1;
    uint32_t has_point_coordinates : 1;
    uint32_t point_ps_ucp_mode : 2;
  };
};
void CreateDxbcGeometryShader(GeometryShaderKey key, std::vector<uint32_t>& shader_out);
inline void SetPointConstants(const rex::xenos::RegisterFile& r,
    const rex::gpu::draw_state::HostViewport& viewport,
    rex::gpu::DXBCShaderTranslator::SystemConstants& c) {
  using namespace rex::gpu;
using namespace rex::xenos;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
  const auto limits = r.Get<reg::PA_SU_POINT_MINMAX>();
  const auto size = r.Get<reg::PA_SU_POINT_SIZE>();
  c.point_vertex_diameter_min = float(limits.min_size) * 0.125f;
  c.point_vertex_diameter_max = float(limits.max_size) * 0.125f;
  c.point_constant_diameter[0] = float(size.width) * 0.125f;
  c.point_constant_diameter[1] = float(size.height) * 0.125f;
  for (unsigned i = 0; i < 2; ++i)
    c.point_screen_diameter_to_ndc_radius[i] = 1.0f / std::max(viewport.xy_extent[i], 1u);
}
}
