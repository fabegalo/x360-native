#include "vulkan_shader_cache.h"

#include <gpu/xenos/xenos_settings.h>
#include <rex/logging/log_category.h>
#include <xxhash.h>
#include <cstring>
#include <stdexcept>

namespace x360n::native {
using namespace rex::gpu;
using namespace rex::xenos;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
VulkanShaderCache::VulkanShaderCache(const XenosSettings& settings, const rex::gpu::vulkan::VulkanDevice& device,
                                     bool edram_interlock, uint32_t scale_x, uint32_t scale_y, uint32_t edram_tile_count)
    : settings_(settings), translator(SPIRVShaderTranslator::Features(&device),
                 (device.GetProperties().framebufferColorSampleCounts &
                  device.GetProperties().framebufferDepthSampleCounts &
                  device.GetProperties().framebufferStencilSampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0,
                 (device.GetProperties().framebufferNoAttachmentsSampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0,
                 edram_interlock, scale_x, scale_y,
                 settings.draw_resolution_scaled_texture_offsets.Get(), settings.mulsc_round_toward_zero.Get(),
                 edram_tile_count, settings.texture_point_snap.Get()), device_(device) {
  if (edram_interlock && !device.GetProperties().fragmentShaderSampleInterlock)
    throw std::runtime_error("Native Vulkan: EDRAM requires fragment shader sample interlock");
}
VulkanShaderCache::~VulkanShaderCache() {
  for (const auto& [translation, module] : modules_)
    device_.GetFunctions().vkDestroyShaderModule(device_.GetDevice(), module, nullptr);
}
SPIRVShader* VulkanShaderCache::Load(xenos::ShaderType type, const uint32_t* words, uint32_t count) {
  if (!words || !count || count > 65536 ||
      (type != xenos::ShaderType::kVertex && type != xenos::ShaderType::kPixel))
    throw std::invalid_argument("Native Vulkan: invalid guest shader");
  const auto hash = XXH3_64bits(words, size_t(count) * sizeof(uint32_t));
  const auto range = shaders_.equal_range(hash);
  for (auto it = range.first; it != range.second; ++it) {
    auto& shader = *it->second;
    if (shader.GetType() != type || shader.GetUcodeDwordCount() != count) continue;
    bool equal = true;
    for (uint32_t i = 0; i < count; ++i)
      if (shader.GetUcodeDwords()[i] != rex::byte_swap(words[i])) { equal = false; break; }
    if (equal) return &shader;
  }
  auto shader = std::make_unique<SPIRVShader>(type, hash, words, count);
  rex::string::StringBuffer disassembly;
  shader->AnalyzeUcode(disassembly, settings_.dump_shaders.Get());
  auto* result = shader.get();
  shaders_.emplace(hash, std::move(shader));
  return result;
}
VulkanShaderCache::Translation VulkanShaderCache::Translate(SPIRVShader& shader, rex::gpu::ShaderModificationKey modification) {
  bool created = false;
  auto* translated = shader.GetOrCreateTranslation(modification, &created);
  if (created && !translator.TranslateAnalyzedShader(*translated))
    throw std::runtime_error("Native Vulkan: guest shader translation failed");
  if (!translated->IsValid()) throw std::runtime_error("Native Vulkan: invalid guest shader translation");
  if (const auto it = modules_.find(translated); it != modules_.end())
    return {translated, it->second};
  const auto& binary = translated->GetTranslatedBinary();
  if (binary.size() % sizeof(uint32_t))
    throw std::runtime_error("Native Vulkan: non-word-aligned guest shader SPIR-V");
  // Shader's binary is byte storage. Keep Vulkan's pCode alignment contract
  // independently of the allocator's current alignment for vector<uint8_t>.
  std::vector<uint32_t> words(binary.size() / sizeof(uint32_t));
  std::memcpy(words.data(), binary.data(), binary.size());
  // Persist the translation before entering the driver, including when driver
  // compilation terminates the process before vkCreateShaderModule returns.
  if (created && !settings_.dump_shaders.Get().empty())
    translated->DumpTranslation(settings_.dump_shaders.Get(), "native-vulkan");
  const auto module = CreateNativeShaderModule(device_, words);
  try { modules_.emplace(translated, module); } catch (...) {
    device_.GetFunctions().vkDestroyShaderModule(device_.GetDevice(), module, nullptr);
    throw;
  }
  return {translated, module};
}
}
