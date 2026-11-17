#include "shader_cache.h"

#include <gpu/xenos/xenos_settings.h>
#include <rex/logging/log_category.h>
#include <xxhash.h>
#include <stdexcept>
#include <bit>
namespace x360n::native {
using namespace rex::gpu;
using namespace rex::xenos;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
namespace {
DXBCOptions ShaderOptions(const XenosSettings& settings, const d3d12::D3D12Settings& api) {
  DXBCOptions options;
  options.source_map = api.dxbc_source_map.Get();
  options.use_switch = api.dxbc_switch.Get();
  options.fuzzy_alpha_epsilon = api.use_fuzzy_alpha_epsilon.Get();
  options.unorm8_fixed16 = api.dxbc_unorm8_fixed16.Get();
  options.scaled_texture_offsets = settings.draw_resolution_scaled_texture_offsets.Get();
  options.texture_point_snap = settings.texture_point_snap.Get();
  return options;
}
}
ShaderCache::ShaderCache(const XenosSettings& settings, const d3d12::D3D12Settings& api,
    uint32_t x, uint32_t y, uint32_t edram_tile_count)
    : settings_(settings), translator(settings, ShaderOptions(settings, api),
          GraphicsProvider::GPUVendorID(0), true, true, false, true, x, y, false, edram_tile_count) {}
DXBCShader* ShaderCache::Load(xenos::ShaderType type, const uint32_t* data, uint32_t count) {
  if (!count || count > 65536 || (type != xenos::ShaderType::kVertex && type != xenos::ShaderType::kPixel))
    throw std::runtime_error("Native: invalid guest shader");
  const auto hash = XXH3_64bits(data, count * 4);
  const auto range = shaders_.equal_range(hash);
  for (auto i = range.first; i != range.second; ++i) {
    auto& s = *i->second;
    if (s.GetType() != type || s.GetUcodeDwordCount() != count) continue;
    bool equal = true;
    for (uint32_t j = 0; j < count; ++j) equal &= s.GetUcodeDwords()[j] == rex::byte_swap(data[j]);
    if (equal) return &s;
  }
  auto shader = std::make_unique<DXBCShader>(type, hash, data, count);
  rex::string::StringBuffer disassembly;
  shader->AnalyzeUcode(disassembly, settings_.dump_shaders.Get());
  REXLOG_INFO("Native shader {:016X} type={} dwords={} vfetch={} textures={} exports={:X}",
      hash, uint32_t(type), count, std::popcount(shader->GetConstantRegisterMap().vertex_fetch_bitmap[0]) +
          std::popcount(shader->GetConstantRegisterMap().vertex_fetch_bitmap[1]) +
          std::popcount(shader->GetConstantRegisterMap().vertex_fetch_bitmap[2]), shader->GetTextureBindings().size(), shader->GetWritesColorTargets());
  auto* result = shader.get();
  shaders_.emplace(hash, std::move(shader));
  return result;
}
Shader::Translation* ShaderCache::Translate(DXBCShader& shader, rex::gpu::ShaderModificationKey modification) {
  bool created = false;
  auto* result = shader.GetOrCreateTranslation(modification, &created);
  if (created && !translator.TranslateAnalyzedShader(*result))
    throw std::runtime_error("Native: guest shader translation failed");
  if (!result->IsValid()) throw std::runtime_error("Native: invalid translated shader");
  if (created && !settings_.dump_shaders.Get().empty()) result->DumpTranslation(settings_.dump_shaders.Get(), "native");
  return result;
}
}
