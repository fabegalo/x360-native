#pragma once
#include "../commands/tessellation.h"
#include <d3d12.h>
namespace x360n::native {
namespace tessellation_bytecode {
#include "src/graphics/shaders/bytecode/d3d12_5_1/tessellation_indexed_vs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/tessellation_adaptive_vs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/discrete_triangle_3cp_hs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/discrete_triangle_1cp_hs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/discrete_quad_4cp_hs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/discrete_quad_1cp_hs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/continuous_triangle_3cp_hs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/continuous_triangle_1cp_hs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/continuous_quad_4cp_hs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/continuous_quad_1cp_hs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/adaptive_triangle_hs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/adaptive_quad_hs.h"
}
inline void SetTessellationStages(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd, const Tessellation& t) {
  using namespace tessellation_bytecode;
  using H = Tessellation::H;
  pd.DS = pd.VS;
  pd.VS = t.adaptive ? D3D12_SHADER_BYTECODE{tessellation_adaptive_vs, sizeof(tessellation_adaptive_vs)}
                     : D3D12_SHADER_BYTECODE{tessellation_indexed_vs, sizeof(tessellation_indexed_vs)};
  const bool triangle = t.host == H::kTriangleDomainCPIndexed || t.host == H::kTriangleDomainPatchIndexed;
  const bool patch = t.host == H::kTriangleDomainPatchIndexed || t.host == H::kQuadDomainPatchIndexed;
  if (t.adaptive) {
    pd.HS = triangle ? D3D12_SHADER_BYTECODE{adaptive_triangle_hs, sizeof(adaptive_triangle_hs)}
                     : D3D12_SHADER_BYTECODE{adaptive_quad_hs, sizeof(adaptive_quad_hs)};
  } else if (t.mode == rex::graphics::xenos::TessellationMode::kContinuous) {
    if (triangle) pd.HS = patch ? D3D12_SHADER_BYTECODE{continuous_triangle_1cp_hs, sizeof(continuous_triangle_1cp_hs)} : D3D12_SHADER_BYTECODE{continuous_triangle_3cp_hs, sizeof(continuous_triangle_3cp_hs)};
    else pd.HS = patch ? D3D12_SHADER_BYTECODE{continuous_quad_1cp_hs, sizeof(continuous_quad_1cp_hs)} : D3D12_SHADER_BYTECODE{continuous_quad_4cp_hs, sizeof(continuous_quad_4cp_hs)};
  } else {
    if (triangle) pd.HS = patch ? D3D12_SHADER_BYTECODE{discrete_triangle_1cp_hs, sizeof(discrete_triangle_1cp_hs)} : D3D12_SHADER_BYTECODE{discrete_triangle_3cp_hs, sizeof(discrete_triangle_3cp_hs)};
    else pd.HS = patch ? D3D12_SHADER_BYTECODE{discrete_quad_1cp_hs, sizeof(discrete_quad_1cp_hs)} : D3D12_SHADER_BYTECODE{discrete_quad_4cp_hs, sizeof(discrete_quad_4cp_hs)};
  }
}
}
