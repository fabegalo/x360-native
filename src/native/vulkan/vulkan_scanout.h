#pragma once
#include "vulkan_core.h"
#include "vulkan_fxaa.h"
#include "../commands/scanout_backend.h"
#include <rex/ui/vulkan/provider.h>

namespace x360n::native {
// Presentation component for the Native Vulkan executor. It implements the
// existing ScanoutTexture/ScanoutOverlay contract. VulkanBackend combines it
// with Native's rendering executor; both own their command and resource state.
class VulkanScanout {
 public:
  explicit VulkanScanout(rex::ui::vulkan::VulkanProvider&);
  ~VulkanScanout();
  VulkanScanout(const VulkanScanout&) = delete;
  VulkanScanout& operator=(const VulkanScanout&) = delete;
  void Present(rex::ui::Presenter&, const ScanoutTexture&, const void* pixels,
               const uint32_t* lut, const ScanoutOverlay*, const void* overlay_pixels,
               ScanoutPostEffect = ScanoutPostEffect::kNone,
               uint32_t display_width = 0, uint32_t display_height = 0);
  void Drain() { submissions_.Drain(); }
 private:
  void Initialize();
  void Destroy();
  const rex::ui::vulkan::VulkanDevice& device_;
  VulkanSubmissionRing submissions_;
  VkDescriptorSetLayout descriptors_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptors_pool_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  std::array<VkRenderPass, 2> render_pass_{};
  std::array<VkPipeline, 2> pipeline_{};
  std::unique_ptr<VulkanFxaa> fxaa_;
  struct Slot {
    std::unique_ptr<VulkanBuffer> upload;
    VkDescriptorSet descriptors = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    uint64_t image_version = UINT64_MAX;
    uint32_t width = 0, height = 0; bool fxaa = false;
  };
  std::array<Slot, VulkanSubmissionRing::kSlots> slots_;
};
}
