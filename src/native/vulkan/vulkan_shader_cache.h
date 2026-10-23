#pragma once
#include "vulkan_core.h"
#include <rex/graphics/pipeline/shader/spirv.h>
#include <unordered_map>

namespace x360n::native {
// Shares guest instruction semantics only. Translation ownership, module
// lifetime and cache identity belong to Native, independently of Xenos.
class VulkanShaderCache {
 public:
  explicit VulkanShaderCache(const rex::ui::vulkan::VulkanDevice&, bool edram_interlock);
  ~VulkanShaderCache();
  VulkanShaderCache(const VulkanShaderCache&) = delete;
  VulkanShaderCache& operator=(const VulkanShaderCache&) = delete;
  rex::graphics::SpirvShader* Load(rex::graphics::xenos::ShaderType,
                                  const uint32_t* big_endian_words, uint32_t count);
  struct Translation {
    rex::graphics::Shader::Translation* guest;
    VkShaderModule module;
  };
  Translation Translate(rex::graphics::SpirvShader&, uint64_t modification);
  rex::graphics::SpirvShaderTranslator translator;
 private:
  const rex::ui::vulkan::VulkanDevice& device_;
  std::unordered_multimap<uint64_t, std::unique_ptr<rex::graphics::SpirvShader>> shaders_;
  std::unordered_map<rex::graphics::Shader::Translation*, VkShaderModule> modules_;
};
}
