#pragma once
#include "../commands/tessellation.h"
#include <d3d12.h>
namespace x360n::native {
namespace tessellation_bytecode {
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/tessellation_indexed_vs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/tessellation_adaptive_vs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/discrete_triangle_3cp_hs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/discrete_triangle_1cp_hs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/discrete_quad_4cp_hs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/discrete_quad_1cp_hs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/continuous_triangle_3cp_hs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/continuous_triangle_1cp_hs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/continuous_quad_4cp_hs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/continuous_quad_1cp_hs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/adaptive_triangle_hs.h"
#include "src/gpu/xenos/shaders/bytecode/d3d12_5_1/adaptive_quad_hs.h"
}
inline void SetTessellationStages(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd, const Tessellation& t) {
  using namespace tessellation_bytecode;
  using H = Tessellation::H;
  pd.DS = pd.VS;
  pd.VS = t.adaptive ? D3D12_SHADER_BYTECODE{kTessellationAdaptiveVS, sizeof(kTessellationAdaptiveVS)}
                     : D3D12_SHADER_BYTECODE{kTessellationIndexedVS, sizeof(kTessellationIndexedVS)};
  const bool triangle = t.host == H::kTriangleDomainCPIndexed || t.host == H::kTriangleDomainPatchIndexed;
  const bool patch = t.host == H::kTriangleDomainPatchIndexed || t.host == H::kQuadDomainPatchIndexed;
  if (t.adaptive) {
    pd.HS = triangle ? D3D12_SHADER_BYTECODE{kAdaptiveTriangleHS, sizeof(kAdaptiveTriangleHS)}
                     : D3D12_SHADER_BYTECODE{kAdaptiveQuadHS, sizeof(kAdaptiveQuadHS)};
  } else if (t.mode == rex::xenos::TessellationMode::kContinuous) {
    if (triangle) pd.HS = patch ? D3D12_SHADER_BYTECODE{kContinuousTriangle1CPHS, sizeof(kContinuousTriangle1CPHS)} : D3D12_SHADER_BYTECODE{kContinuousTriangle3CPHS, sizeof(kContinuousTriangle3CPHS)};
    else pd.HS = patch ? D3D12_SHADER_BYTECODE{kContinuousQuad1CPHS, sizeof(kContinuousQuad1CPHS)} : D3D12_SHADER_BYTECODE{kContinuousQuad4CPHS, sizeof(kContinuousQuad4CPHS)};
  } else {
    if (triangle) pd.HS = patch ? D3D12_SHADER_BYTECODE{kDiscreteTriangle1CPHS, sizeof(kDiscreteTriangle1CPHS)} : D3D12_SHADER_BYTECODE{kDiscreteTriangle3CPHS, sizeof(kDiscreteTriangle3CPHS)};
    else pd.HS = patch ? D3D12_SHADER_BYTECODE{kDiscreteQuad1CPHS, sizeof(kDiscreteQuad1CPHS)} : D3D12_SHADER_BYTECODE{kDiscreteQuad4CPHS, sizeof(kDiscreteQuad4CPHS)};
  }
}
}
