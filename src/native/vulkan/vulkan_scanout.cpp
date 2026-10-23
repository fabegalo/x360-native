#include <rex/chrono/frame_layout.h>
#include "vulkan_scanout.h"
#include "vulkan_scanout_source.h"
#include <rex/ui/vulkan/presenter.h>
#include <rex/logging.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace x360n::native {
using Presenter = rex::ui::vulkan::VulkanPresenter;
VulkanScanout::VulkanScanout(rex::ui::vulkan::VulkanProvider& provider)
    : device_(*provider.vulkan_device()), submissions_(device_) {
  try { Initialize(); } catch (...) { Destroy(); throw; }
}
VulkanScanout::~VulkanScanout() {
  try { Drain(); } catch (const std::exception& error) {
    REX_FATAL("Native Vulkan: unsafe scanout teardown: {}", error.what());
  }
  Destroy();
}
void VulkanScanout::Destroy() {
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  for (auto& slot : slots_) {
    if (slot.framebuffer) fn.vkDestroyFramebuffer(vk, slot.framebuffer, nullptr);
    slot.framebuffer = VK_NULL_HANDLE;
    slot.upload.reset();
  }
  fxaa_.reset();
  for (auto pipeline : pipeline_) if (pipeline) fn.vkDestroyPipeline(vk, pipeline, nullptr);
  for (auto pass : render_pass_) if (pass) fn.vkDestroyRenderPass(vk, pass, nullptr);
  if (pipeline_layout_) fn.vkDestroyPipelineLayout(vk, pipeline_layout_, nullptr);
  if (descriptors_pool_) fn.vkDestroyDescriptorPool(vk, descriptors_pool_, nullptr);
  if (descriptors_layout_) fn.vkDestroyDescriptorSetLayout(vk, descriptors_layout_, nullptr);
  pipeline_.fill(VK_NULL_HANDLE);
  render_pass_.fill(VK_NULL_HANDLE);
  pipeline_layout_ = VK_NULL_HANDLE;
  descriptors_pool_ = VK_NULL_HANDLE;
  descriptors_layout_ = VK_NULL_HANDLE;
}
void VulkanScanout::Initialize() {
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  VkDescriptorSetLayoutBinding bindings[3]{};
  for (uint32_t i = 0; i < 3; ++i)
    bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set.bindingCount = 3;
  set.pBindings = bindings;
  CheckVulkan(fn.vkCreateDescriptorSetLayout(vk, &set, nullptr, &descriptors_layout_), "create scanout descriptor layout");
  const VkPushConstantRange constants{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 24 * sizeof(uint32_t)};
  VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout.setLayoutCount = 1;
  layout.pSetLayouts = &descriptors_layout_;
  layout.pushConstantRangeCount = 1;
  layout.pPushConstantRanges = &constants;
  CheckVulkan(fn.vkCreatePipelineLayout(vk, &layout, nullptr, &pipeline_layout_), "create scanout pipeline layout");
  const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uint32_t(slots_.size()) * 3};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.maxSets = uint32_t(slots_.size());
  pool.poolSizeCount = 1;
  pool.pPoolSizes = &pool_size;
  CheckVulkan(fn.vkCreateDescriptorPool(vk, &pool, nullptr, &descriptors_pool_), "create scanout descriptor pool");
  for (auto& slot : slots_) {
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = descriptors_pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &descriptors_layout_;
    CheckVulkan(fn.vkAllocateDescriptorSets(vk, &allocate, &slot.descriptors), "allocate scanout descriptors");
  }

  VkAttachmentDescription attachment{};
  attachment.format = Presenter::kGuestOutputFormat;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color;
  VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  pass.attachmentCount = 1;
  pass.pAttachments = &attachment;
  pass.subpassCount = 1;
  pass.pSubpasses = &subpass;
  for (unsigned i=0;i<2;++i) {
    attachment.format = i ? VulkanFxaa::kFormat : Presenter::kGuestOutputFormat;
    CheckVulkan(fn.vkCreateRenderPass(vk, &pass, nullptr, &render_pass_[i]), "create scanout render pass");
  }

  VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
  try {
    vertex = CreateNativeShaderModule(device_, CompileNativeGlsl(VK_SHADER_STAGE_VERTEX_BIT, kNativeScanoutVertex));
    fragment = CreateNativeShaderModule(device_, CompileNativeGlsl(VK_SHADER_STAGE_FRAGMENT_BIT, kNativeScanoutFragment));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";
    const VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    const VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &samples;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipeline_layout_;
    for (unsigned i=0;i<2;++i) {
      info.renderPass = render_pass_[i];
      CheckVulkan(fn.vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_[i]), "create scanout graphics pipeline");
    }
  } catch (...) {
    if (vertex) fn.vkDestroyShaderModule(vk, vertex, nullptr);
    if (fragment) fn.vkDestroyShaderModule(vk, fragment, nullptr);
    throw;
  }
  fn.vkDestroyShaderModule(vk, vertex, nullptr);
  fn.vkDestroyShaderModule(vk, fragment, nullptr);
}

void VulkanScanout::Present(rex::ui::Presenter& presenter, const ScanoutTexture& texture,
                           const void* pixels, const uint32_t* lut, const ScanoutOverlay* overlay,
                           const void* overlay_pixels, ScanoutPostEffect post_effect, uint32_t display_width, uint32_t display_height) {
  if (!pixels || !lut || !texture.width || !texture.height || !texture.bytes ||
      (overlay && (!overlay_pixels || !overlay->surface.bytes ||
                   !overlay->display_width || !overlay->display_height)))
    throw std::invalid_argument("Native Vulkan: invalid scanout input");
  if (texture.width > device_.properties().maxFramebufferWidth ||
      texture.height > device_.properties().maxFramebufferHeight ||
      texture.bytes > device_.properties().maxStorageBufferRange ||
      (overlay && overlay->surface.bytes > device_.properties().maxStorageBufferRange))
    throw std::out_of_range("Native Vulkan: scanout exceeds host limits");
  const bool fxaa = post_effect != ScanoutPostEffect::kNone;
  if (fxaa && !fxaa_) fxaa_ = std::make_unique<VulkanFxaa>(device_);
  const auto slot_index = submissions_.current_slot();
  const auto commands = submissions_.Begin();
  auto& slot = slots_[slot_index];
  const auto align = [alignment = device_.properties().minStorageBufferOffsetAlignment](uint64_t value) {
    return ((value + alignment - 1) / alignment) * alignment;
  };
  const uint64_t lut_offset = align(texture.bytes);
  const uint32_t lut_bytes = uint32_t(texture.rgb10 ? 128 * 3 : 256) * sizeof(uint32_t);
  const uint64_t overlay_offset = align(lut_offset + lut_bytes);
  const uint64_t required = overlay_offset + (overlay ? overlay->surface.bytes : 0);
  if (!slot.upload || slot.upload->size() < required)
    slot.upload = std::make_unique<VulkanBuffer>(device_, required, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                               VulkanBuffer::Access::kUpload);
  std::memcpy(slot.upload->mapped(), pixels, texture.bytes);
  std::memcpy(slot.upload->mapped() + lut_offset, lut, lut_bytes);
  if (overlay) std::memcpy(slot.upload->mapped() + overlay_offset, overlay_pixels, overlay->surface.bytes);
  slot.upload->Flush(0, required);
  const VkDescriptorBufferInfo buffers[]{
      {slot.upload->handle(), 0, texture.bytes},
      {slot.upload->handle(), lut_offset, lut_bytes},
      {slot.upload->handle(), overlay ? overlay_offset : 0, overlay ? overlay->surface.bytes : texture.bytes}};
  VkWriteDescriptorSet writes[3]{};
  for (uint32_t i = 0; i < 3; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = slot.descriptors;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = &buffers[i];
  }
  const auto& fn = device_.functions();
  fn.vkUpdateDescriptorSets(device_.device(), 3, writes, 0, nullptr);
  const auto display_aspect = rex::chrono::FrameLayout::DisplayAspect();
  const uint32_t aspect_x = display_aspect.x ? display_aspect.x : (display_width ? display_width : texture.width);
  const uint32_t aspect_y = display_aspect.y ? display_aspect.y : (display_height ? display_height : texture.height);
  if (!presenter.RefreshGuestOutput(texture.width, texture.height, aspect_x, aspect_y,
      [&](rex::ui::Presenter::GuestOutputRefreshContext& base_context) {
    auto& context = static_cast<Presenter::VulkanGuestOutputRefreshContext&>(base_context);
    context.SetIs8bpc(!texture.rgb10);
    const bool recreate = slot.image_version != context.image_version() || slot.fxaa != fxaa ||
        slot.width != texture.width || slot.height != texture.height;
    if (recreate && slot.framebuffer) { fn.vkDestroyFramebuffer(device_.device(), slot.framebuffer, nullptr); slot.framebuffer = VK_NULL_HANDLE; }
    const auto gamma_view = fxaa ? fxaa_->Begin(slot_index, texture.width, texture.height, commands) : context.image_view();
    if (recreate) {
      if (slot.framebuffer) fn.vkDestroyFramebuffer(device_.device(), slot.framebuffer, nullptr);
      slot.framebuffer = VK_NULL_HANDLE;
      const auto view = gamma_view;
      VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      info.renderPass = render_pass_[fxaa];
      info.attachmentCount = 1;
      info.pAttachments = &view;
      info.width = texture.width;
      info.height = texture.height;
      info.layers = 1;
      CheckVulkan(fn.vkCreateFramebuffer(device_.device(), &info, nullptr, &slot.framebuffer), "create scanout framebuffer");
      slot.image_version = context.image_version(); slot.width = texture.width; slot.height = texture.height; slot.fxaa = fxaa;
    }
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = context.image_ever_written_previously() ? Presenter::kGuestOutputInternalAccessMask : 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = context.image();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!fxaa) fn.vkCmdPipelineBarrier(commands,
        context.image_ever_written_previously() ? Presenter::kGuestOutputInternalStageMask : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = render_pass_[fxaa];
    pass.framebuffer = slot.framebuffer;
    pass.renderArea = {{0, 0}, {texture.width, texture.height}};
    fn.vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
    fn.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_[fxaa]);
    fn.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &slot.descriptors, 0, nullptr);
    const VkViewport viewport{0.0f, 0.0f, float(texture.width), float(texture.height), 0.0f, 1.0f};
    fn.vkCmdSetViewport(commands, 0, 1, &viewport);
    fn.vkCmdSetScissor(commands, 0, 1, &pass.renderArea);
    uint32_t constants[24]{texture.width, texture.height, texture.pitch, texture.tiled,
                           texture.endian, texture.swizzle, uint32_t(texture.rgb10), uint32_t(fxaa)};
    if (overlay) {
      const auto& o = *overlay;
      const uint32_t plane[]{1, o.surface.width, o.surface.height, o.surface.pitch,
          o.surface.tiled, o.surface.endian, o.surface.swizzle, uint32_t(o.surface.rgb10),
          o.x, o.y, o.right, o.bottom, o.offset_x, o.offset_y};
      const float scale[]{float(o.display_width) / texture.width, float(o.display_height) / texture.height};
      std::memcpy(constants + 8, plane, sizeof(plane));
      std::memcpy(constants + 22, scale, sizeof(scale));
    }
    fn.vkCmdPushConstants(commands, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), constants);
    fn.vkCmdDraw(commands, 3, 1, 0, 0);
    fn.vkCmdEndRenderPass(commands);
    if (fxaa) fxaa_->Apply(slot_index, commands, context.image(), context.image_view(),
        context.image_ever_written_previously(), post_effect == ScanoutPostEffect::kFxaaExtreme);
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = Presenter::kGuestOutputInternalAccessMask;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = Presenter::kGuestOutputInternalLayout;
    if (!fxaa) fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        Presenter::kGuestOutputInternalStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    submissions_.Submit();
    return true;
  })) throw std::runtime_error("Native Vulkan: presenter rejected guest frame");
}
}
