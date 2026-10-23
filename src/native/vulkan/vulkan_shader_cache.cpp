#include "vulkan_shader_cache.h"
#include <rex/cvar.h>
#include <rex/graphics/flags.h>
#include <rex/logging.h>
#include <xxhash.h>
#include <cstring>
#include <stdexcept>

namespace x360n::native {
using namespace rex::graphics;
VulkanShaderCache::VulkanShaderCache(const rex::ui::vulkan::VulkanDevice& device,
                                     bool edram_interlock, uint32_t scale_x, uint32_t scale_y)
    : translator(SpirvShaderTranslator::Features(&device),
                 (device.properties().framebufferColorSampleCounts &
                  device.properties().framebufferDepthSampleCounts &
                  device.properties().framebufferStencilSampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0,
                 (device.properties().framebufferNoAttachmentsSampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0,
                 edram_interlock, scale_x, scale_y), device_(device) {
  if (edram_interlock && !device.properties().fragmentShaderSampleInterlock)
    throw std::runtime_error("Native Vulkan: EDRAM requires fragment shader sample interlock");
}
VulkanShaderCache::~VulkanShaderCache() {
  for (const auto& [translation, module] : modules_)
    device_.functions().vkDestroyShaderModule(device_.device(), module, nullptr);
}
SpirvShader* VulkanShaderCache::Load(xenos::ShaderType type, const uint32_t* words, uint32_t count) {
  if (!words || !count || count > 65536 ||
      (type != xenos::ShaderType::kVertex && type != xenos::ShaderType::kPixel))
    throw std::invalid_argument("Native Vulkan: invalid guest shader");
  const auto hash = XXH3_64bits(words, size_t(count) * sizeof(uint32_t));
  const auto range = shaders_.equal_range(hash);
  for (auto it = range.first; it != range.second; ++it) {
    auto& shader = *it->second;
    if (shader.type() != type || shader.ucode_dword_count() != count) continue;
    bool equal = true;
    for (uint32_t i = 0; i < count; ++i)
      if (shader.ucode_dwords()[i] != rex::byte_swap(words[i])) { equal = false; break; }
    if (equal) return &shader;
  }
  auto shader = std::make_unique<SpirvShader>(type, hash, words, count);
  rex::string::StringBuffer disassembly;
  shader->AnalyzeUcode(disassembly);
  if (!REXCVAR_GET(dump_shaders).empty()) shader->DumpUcode(REXCVAR_GET(dump_shaders));
  auto* result = shader.get();
  shaders_.emplace(hash, std::move(shader));
  return result;
}
VulkanShaderCache::Translation VulkanShaderCache::Translate(SpirvShader& shader, uint64_t modification) {
  bool created = false;
  auto* translated = shader.GetOrCreateTranslation(modification, &created);
  if (created && !translator.TranslateAnalyzedShader(*translated))
    throw std::runtime_error("Native Vulkan: guest shader translation failed");
  if (!translated->is_valid()) throw std::runtime_error("Native Vulkan: invalid guest shader translation");
  if (const auto it = modules_.find(translated); it != modules_.end())
    return {translated, it->second};
  const auto& binary = translated->translated_binary();
  if (binary.size() % sizeof(uint32_t))
    throw std::runtime_error("Native Vulkan: non-word-aligned guest shader SPIR-V");
  // Shader's binary is byte storage. Keep Vulkan's pCode alignment contract
  // independently of the allocator's current alignment for vector<uint8_t>.
  std::vector<uint32_t> words(binary.size() / sizeof(uint32_t));
  std::memcpy(words.data(), binary.data(), binary.size());
  const auto module = CreateNativeShaderModule(device_, words);
  try { modules_.emplace(translated, module); } catch (...) {
    device_.functions().vkDestroyShaderModule(device_.device(), module, nullptr);
    throw;
  }
  if (created && !REXCVAR_GET(dump_shaders).empty())
    translated->Dump(REXCVAR_GET(dump_shaders), "native-vulkan");
  return {translated, module};
}
}
