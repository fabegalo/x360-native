#pragma once
#include <rex/graphics/pipeline/shader/dxbc.h>
#include <unordered_map>
#include <memory>
namespace x360n::native {
class ShaderCache {
 public:
  ShaderCache(uint32_t x = 1, uint32_t y = 1, uint32_t edram_tile_count = rex::graphics::xenos::kEdramTileCount) : translator(rex::ui::GraphicsProvider::GpuVendorID(0), true, true, false, true, x, y, false, edram_tile_count) {}
  rex::graphics::DxbcShader* Load(rex::graphics::xenos::ShaderType, const uint32_t*, uint32_t);
  rex::graphics::Shader::Translation* Translate(rex::graphics::DxbcShader&, rex::graphics::ShaderModificationKey modification);
  rex::graphics::DxbcShaderTranslator translator;
 private:
  std::unordered_multimap<uint64_t, std::unique_ptr<rex::graphics::DxbcShader>> shaders_;
};
}
