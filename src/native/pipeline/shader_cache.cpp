#include "shader_cache.h"
#include <rex/cvar.h>
#include <rex/graphics/flags.h>
#include <rex/logging.h>
#include <xxhash.h>
#include <stdexcept>
namespace x360n::native {
using namespace rex::graphics;
DxbcShader* ShaderCache::Load(xenos::ShaderType type, const uint32_t* data, uint32_t count) {
  if (!count || count > 65536 || (type != xenos::ShaderType::kVertex && type != xenos::ShaderType::kPixel))
    throw std::runtime_error("Native: invalid guest shader");
  const auto hash = XXH3_64bits(data, count * 4);
  const auto range = shaders_.equal_range(hash);
  for (auto i = range.first; i != range.second; ++i) {
    auto& s = *i->second;
    if (s.type() != type || s.ucode_dword_count() != count) continue;
    bool equal = true;
    for (uint32_t j = 0; j < count; ++j) equal &= s.ucode_dwords()[j] == rex::byte_swap(data[j]);
    if (equal) return &s;
  }
  auto shader = std::make_unique<DxbcShader>(type, hash, data, count);
  rex::string::StringBuffer disassembly;
  shader->AnalyzeUcode(disassembly);
  if (!REXCVAR_GET(dump_shaders).empty()) shader->DumpUcode(REXCVAR_GET(dump_shaders));
  REXLOG_INFO("Native shader {:016X} type={} dwords={} vfetch={} textures={} exports={:X}",
      hash, uint32_t(type), count, shader->vertex_bindings().size(), shader->texture_bindings().size(), shader->writes_color_targets());
  auto* result = shader.get();
  shaders_.emplace(hash, std::move(shader));
  return result;
}
Shader::Translation* ShaderCache::Translate(DxbcShader& shader, uint64_t modification) {
  bool created = false;
  auto* result = shader.GetOrCreateTranslation(modification, &created);
  if (created && !translator.TranslateAnalyzedShader(*result))
    throw std::runtime_error("Native: guest shader translation failed");
  if (!result->is_valid()) throw std::runtime_error("Native: invalid translated shader");
  if (created && !REXCVAR_GET(dump_shaders).empty()) result->Dump(REXCVAR_GET(dump_shaders), "native");
  return result;
}
}
