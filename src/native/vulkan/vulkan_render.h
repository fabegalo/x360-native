#pragma once
#include "../quality/settings.h"
#include "../memory/scaled_resolve.h"
#include "vulkan_core.h"
#include "vulkan_shader_cache.h"
#include "../commands/tessellation.h"
#include "../memory/guest_memory_mirror.h"
#include "../memory/guest_writeback.h"
#include "../commands/scanout_backend.h"
#include <rex/graphics/util/draw.h>
#include <rex/ui/vulkan/provider.h>
#include <unordered_map>
#include <map>
#include <optional>

namespace x360n::native {
// Native's Vulkan command recording and memory domain. The shared SDK types
// below describe guest registers and shader semantics, never execute Xenos.
class VulkanRender {
  const NativeQualitySettings quality_ = GetNativeQualitySettings();
  const uint32_t scale_x_ = quality_.scale_x, scale_y_ = quality_.scale_y;
  const uint64_t edram_size_ = uint64_t(rex::graphics::xenos::kEdramSizeBytes) * scale_x_ * scale_y_;
 public:
  explicit VulkanRender(rex::ui::vulkan::VulkanProvider&);
  ~VulkanRender();
  VulkanRender(const VulkanRender&) = delete;
  VulkanRender& operator=(const VulkanRender&) = delete;
  rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType, const uint32_t*, uint32_t);
  void Draw(const rex::graphics::RegisterFile&, rex::memory::Memory&, rex::graphics::Shader*,
      rex::graphics::Shader*, rex::graphics::xenos::PrimitiveType, uint32_t, const DrawIndices*, DrawPurpose = DrawPurpose::kGuest);
  bool PrepareScanout(const ScanoutTexture& input, const void* pixels, ScanoutTexture& output,
                       std::vector<uint8_t>& storage) const { return scaled_.Scanout(input, pixels, output, storage); }
  void Resolve(const rex::graphics::RegisterFile&, rex::memory::Memory&);
  SampleCounts ReadSampleCounts(bool counting_after);
  void SetSampleCounts(const SampleCounts&, bool visibility_only);
  void SetPipelineDirectory(const std::filesystem::path&);
  void CompleteGuestWrites(GuestSync, uint32_t) { if (!writeback_.empty()) Flush(true); }
  void SyncGuestRead(uint32_t address, uint32_t length, uint32_t) {
    if (writeback_.Overlaps(address, length)) Flush(true);
  }
  void Flush(bool wait);
  void EndFrame() { if (mirror_) mirror_->InvalidateCpuPages(); }
 private:
  using Device = rex::ui::vulkan::VulkanDevice;
  void Destroy();
  void SavePipelineCache();
  void Begin();
  void EndRendering();
  struct UploadSlice { VkBuffer buffer; VkDeviceSize offset; std::byte* mapped; };
  UploadSlice Upload(const void*, VkDeviceSize size, VkDeviceSize alignment = 16);
  VkDescriptorSet AllocateDescriptors(VkDescriptorSetLayout);
  VkPipeline ComputePipeline(VkPipelineLayout, std::span<const uint32_t>);
  GuestMemoryMirror& Mirror(rex::memory::Memory&);
  void UploadGuest(uint32_t address, uint32_t length);
  struct BufferAccess { VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; VkAccessFlags access = 0; };
  void UseBuffer(VkBuffer, BufferAccess&, VkPipelineStageFlags, VkAccessFlags, bool force = false);
  struct TextureResource;
  struct TextureEntry {
    std::shared_ptr<TextureResource> resource;
    bool outdated = true;
    uint64_t last_use = 0;
    GuestMemoryMirror::WatchHandle watches[2]{};
  };
  static void TextureWatch(void*, void*, uint64_t, bool);
  void Unwatch(TextureEntry&);
  VkDescriptorSetLayout TextureLayout(rex::graphics::SpirvShader*);
  VkDescriptorSet BindTextures(const rex::graphics::RegisterFile&, rex::memory::Memory&,
                               rex::graphics::SpirvShader*);
  void SyncTextureReads(const rex::graphics::RegisterFile&, rex::graphics::SpirvShader*);
  void DestroyTextures();
  void InitializeDraw();
  void DestroyDraw();
  struct DrawPipeline { VkPipelineLayout layout = VK_NULL_HANDLE; VkPipeline pipeline = VK_NULL_HANDLE; };
  DrawPipeline& Pipeline(rex::graphics::SpirvShader*, rex::graphics::SpirvShader*, VkShaderModule,
      VkShaderModule, VkPrimitiveTopology, VkSampleCountFlagBits, VkCullModeFlags, VkFrontFace,
      bool clamp, bool restart, bool discard, const Tessellation&, VkPolygonMode fill, uint32_t point_key);
  const Device& device_;
  VulkanSubmissionRing submissions_;
  VkCommandBuffer commands_ = VK_NULL_HANDLE;
  bool rendering_ = false;
  struct UploadPage { std::unique_ptr<VulkanBuffer> buffer; VkDeviceSize used = 0; };
  struct Arena {
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    uint32_t descriptor_count = 0;
    uint32_t image_descriptors = 0, sampler_descriptors = 0;
    std::vector<UploadPage> uploads;
    size_t upload_index = 0;
  };
  std::array<Arena, VulkanSubmissionRing::kSlots> arenas_;
  std::unique_ptr<VulkanBuffer> guest_buffer_, edram_buffer_, readback_;
  BufferAccess guest_access_, edram_access_;
  std::unique_ptr<GuestMemoryMirror> mirror_;
  ScaledResolveCache scaled_{scale_x_, scale_y_};
  uint32_t textures_resolution_scaled_ = 0;
  rex::memory::Memory* memory_ = nullptr;
  GuestWriteback writeback_;
  uint64_t readback_used_ = 0;
  VkDescriptorSetLayout storage_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout copy_layout_ = VK_NULL_HANDLE, clear_layout_ = VK_NULL_HANDLE;
  VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
  std::filesystem::path pipeline_file_;
  std::array<VkPipeline, size_t(rex::graphics::draw_util::ResolveCopyShaderIndex::kCount)> copy_pipelines_{};
  struct ScaledReadback { ScaledResolveCache::Ticket ticket; std::shared_ptr<VulkanBuffer> buffer;
                          unsigned pixel_log2; uint64_t offset; };
  std::vector<ScaledReadback> scaled_readbacks_;
  VkPipelineLayout downscale_layout_ = VK_NULL_HANDLE;
  VkPipeline downscale_pipeline_ = VK_NULL_HANDLE;
  std::array<VkPipeline, 2> clear_pipelines_{};
  std::unordered_map<std::string, TextureEntry> textures_;
  uint64_t texture_use_ = 0, texture_bytes_ = 0;
  std::map<std::string, VkSampler> samplers_;
  std::unordered_map<const rex::graphics::SpirvShader*, VkDescriptorSetLayout> texture_layouts_;
  std::array<std::shared_ptr<TextureResource>, 3> null_textures_;
  std::unique_ptr<VulkanShaderCache> shaders_;
  std::unordered_map<const rex::graphics::Shader*, bool> memexport_addresses_known_;
  VkDescriptorSetLayout memory_layout_ = VK_NULL_HANDLE, constants_layout_ = VK_NULL_HANDLE;
  VkRenderPass draw_pass_ = VK_NULL_HANDLE;
  VkFramebuffer draw_framebuffer_ = VK_NULL_HANDLE;
  VkExtent2D draw_extent_{};
  std::array<VkShaderModule, 3> depth_only_{};
  bool counting_samples_ = false;
  bool visibility_only_ = false;
  std::unordered_map<std::string, DrawPipeline> draw_pipelines_;
  std::map<uint32_t, VkShaderModule> tessellation_modules_;
  std::optional<std::array<uint32_t, 22>> edram_draw_layout_;
};
}
