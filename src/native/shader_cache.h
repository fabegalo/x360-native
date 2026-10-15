#pragma once
#include <rex/graphics/pipeline/shader/dxbc.h>
#include <unordered_map>
#include <memory>
namespace x360n::native {
class ShaderCache {
 public:
  rex::graphics::DxbcShader* Load(rex::graphics::xenos::ShaderType, const uint32_t*, uint32_t);
  rex::graphics::Shader::Translation* Translate(rex::graphics::DxbcShader&, uint64_t modification);
  rex::graphics::DxbcShaderTranslator translator{rex::ui::GraphicsProvider::GpuVendorID(0), true, true};
 private:
  std::unordered_multimap<uint64_t, std::unique_ptr<rex::graphics::DxbcShader>> shaders_;
};
}
