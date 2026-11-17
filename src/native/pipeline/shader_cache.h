#pragma once
#include <gpu/xenos/pipeline/shader/dxbc.h>
#include <gpu/xenos/host/d3d12/d3d12_settings.h>
#include <unordered_map>
#include <memory>
namespace x360n::native {
class ShaderCache {
  const rex::gpu::XenosSettings& settings_;
 public:
  ShaderCache(const rex::gpu::XenosSettings&, const rex::gpu::d3d12::D3D12Settings&,
      uint32_t x = 1, uint32_t y = 1, uint32_t edram_tile_count = rex::xenos::kEDRAMTileCount);
  rex::gpu::DXBCShader* Load(rex::xenos::ShaderType, const uint32_t*, uint32_t);
  rex::gpu::Shader::Translation* Translate(rex::gpu::DXBCShader&, rex::gpu::ShaderModificationKey modification);
  rex::gpu::DXBCShaderTranslator translator;
 private:
  std::unordered_multimap<uint64_t, std::unique_ptr<rex::gpu::DXBCShader>> shaders_;
};
}
