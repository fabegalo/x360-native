#pragma once
#include "vulkan_core.h"
#include <gpu/xenos/pipeline/shader/spirv.h>
#include <unordered_map>
#include <gpu/xenos/xenos_settings.h>

namespace x360n::native {
// Shares guest instruction semantics only. Translation ownership, module
// lifetime and cache identity belong to Native, independently of Xenos.
class VulkanShaderCache {
  const rex::gpu::XenosSettings& settings_;
 public:
  explicit VulkanShaderCache(const rex::gpu::XenosSettings&, const rex::gpu::vulkan::VulkanDevice&, bool edram_interlock, uint32_t scale_x = 1, uint32_t scale_y = 1, uint32_t edram_tile_count = rex::xenos::kEDRAMTileCount);
  ~VulkanShaderCache();
  VulkanShaderCache(const VulkanShaderCache&) = delete;
  VulkanShaderCache& operator=(const VulkanShaderCache&) = delete;
  rex::gpu::SPIRVShader* Load(rex::xenos::ShaderType,
                                  const uint32_t* big_endian_words, uint32_t count);
  struct Translation {
    rex::gpu::Shader::Translation* guest;
    VkShaderModule module;
  };
  Translation Translate(rex::gpu::SPIRVShader&, rex::gpu::ShaderModificationKey modification);
  rex::gpu::SPIRVShaderTranslator translator;
 private:
  const rex::gpu::vulkan::VulkanDevice& device_;
  std::unordered_multimap<uint64_t, std::unique_ptr<rex::gpu::SPIRVShader>> shaders_;
  std::unordered_map<rex::gpu::Shader::Translation*, VkShaderModule> modules_;
};
}
