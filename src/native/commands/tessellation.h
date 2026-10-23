#pragma once
#include <rex/graphics/register_file.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <stdexcept>
namespace x360n::native {
struct Tessellation {
  using H = rex::graphics::Shader::HostVertexShaderType;
  H host = H::kVertex;
  rex::graphics::xenos::TessellationMode mode{};
  uint32_t control_points = 0;
  bool adaptive = false;
  explicit operator bool() const { return control_points != 0; }
};
inline Tessellation GetTessellation(const rex::graphics::RegisterFile& regs,
    rex::graphics::xenos::PrimitiveType primitive) {
  using namespace rex::graphics;
  using namespace rex::graphics::xenos;
  const auto draw = regs.Get<reg::VGT_DRAW_INITIATOR>();
  Tessellation t;
  if (!IsMajorModeExplicit(draw.major_mode, primitive) ||
      regs.Get<reg::VGT_OUTPUT_PATH_CNTL>().path_select != VGTOutputPath::kTessellationEnable) return t;
  t.mode = regs.Get<reg::VGT_HOS_CNTL>().tess_mode;
  if (t.mode > TessellationMode::kAdaptive) throw std::runtime_error("Native: invalid tessellation mode");
  t.adaptive = t.mode == TessellationMode::kAdaptive;
  switch (primitive) {
    case PrimitiveType::kTriangleList:
      if (t.adaptive) throw std::runtime_error("Native: adaptive tessellation requires patch indices");
      t.host = Tessellation::H::kTriangleDomainCPIndexed; t.control_points = 3; break;
    case PrimitiveType::kQuadList:
      if (t.adaptive) throw std::runtime_error("Native: adaptive tessellation requires patch indices");
      t.host = Tessellation::H::kQuadDomainCPIndexed; t.control_points = 4; break;
    case PrimitiveType::kTrianglePatch:
      t.host = Tessellation::H::kTriangleDomainPatchIndexed; t.control_points = t.adaptive ? 3 : 1; break;
    case PrimitiveType::kQuadPatch:
      t.host = Tessellation::H::kQuadDomainPatchIndexed; t.control_points = t.adaptive ? 4 : 1; break;
    default: throw std::runtime_error("Native: invalid tessellation primitive");
  }
  return t;
}
}
