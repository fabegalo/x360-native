// 0247: bounded headless rasterization of the production Xenos depth-bias
// conversion/dispatch, with real Vulkan feature enablement and depth readback.
// No game, window, or image-quality acceptance is implied by this fixture.
#include <SPIRV/GlslangToSpv.h>
#include <StandAlone/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <rex/graphics/vulkan/depth_bias.h>
#include <rex/ui/vulkan/instance.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace rex::graphics;
using namespace rex::graphics::vulkan;
using namespace rex::ui::vulkan;
namespace {
void Need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<uint32_t> Compile(const char* source, EShLanguage stage) {
  glslang::TShader shader(stage);
  shader.setStrings(&source, 1);
  shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 450);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
  const auto messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
  Need(shader.parse(&glslang::DefaultTBuiltInResource, 450, false, messages), shader.getInfoLog());
  glslang::TProgram program;
  program.addShader(&shader);
  Need(program.link(messages), program.getInfoLog());
  std::vector<uint32_t> output;
  glslang::GlslangToSpv(*program.getIntermediate(stage), output);
  return output;
}
struct Cleanup {
  const VulkanDevice::Functions& fn;
  VulkanDevice& device;
  std::vector<std::function<void()>> actions;
  ~Cleanup() {
    // The only queue used by this fixture; idle it before destroying objects.
    { auto queue = device.AcquireQueue(device.queue_family_graphics_compute(), 0);
      fn.vkQueueWaitIdle(queue.queue()); }
    for (auto action = actions.rbegin(); action != actions.rend(); ++action) (*action)();
  }
};
// RecordHostDepthBias is the production CP dispatch; ExecuteDepthBias2 is
// also the production deferred command executor. Forward both to the driver.
struct DriverCommands {
  const VulkanDevice::Functions& fn;
  VkCommandBuffer command;
  unsigned legacy_calls = 0, constant_calls = 0;
  void CmdVkSetDepthBias(float constant, float clamp, float slope) {
    ++legacy_calls;
    fn.vkCmdSetDepthBias(command, constant, clamp, slope);
  }
  void CmdVkSetDepthBias2EXT(float constant, float clamp, float slope,
                           VkDepthBiasRepresentationEXT representation, bool exact) {
    ++constant_calls;
    Need(fn.vkCmdSetDepthBias2EXT != nullptr, "extension command was not loaded");
    ExecuteDepthBias2(fn, command, constant, clamp, slope, representation, exact);
  }
};

void Run(bool legacy, bool fallback) {
  auto instance = VulkanInstance::Create(false, true);
  Need(bool(instance), "Vulkan instance unavailable");
  std::vector<VkPhysicalDevice> physical;
  instance->EnumeratePhysicalDevices(physical);
  Need(!physical.empty(), "Vulkan GPU unavailable");
  // fallback creates a real device without GPU-emulation optional features.
  auto device = VulkanDevice::CreateIfSupported(instance.get(), physical.front(), !fallback, false);
  Need(bool(device), "Vulkan device creation failed");
  const auto& fn = device->functions();
  const auto& ifn = instance->functions();
  const auto vk = device->device();
  const auto& properties = device->properties();
  std::cout << "GPU=" << properties.deviceName << " depthBiasControl=" << properties.depthBiasControl
            << " forceUnorm=" << properties.leastRepresentableValueForceUnormRepresentation
            << " exact=" << properties.depthBiasExact << " legacy=" << legacy
            << " fallback=" << fallback << '\n';
  if (!fallback) Need(properties.depthBiasControl &&
      properties.leastRepresentableValueForceUnormRepresentation && properties.depthBiasExact,
      "GPU lacks the exact FORCE_UNORM path required by this regression");
  else Need(!properties.depthBiasControl && !device->extensions().ext_EXT_depth_bias_control,
            "fallback device unexpectedly enabled depth-bias extension");
  Cleanup cleanup{fn, *device};
  VkPhysicalDeviceMemoryProperties memory{};
  ifn.vkGetPhysicalDeviceMemoryProperties(physical.front(), &memory);
  auto allocate = [&](VkMemoryRequirements required, VkMemoryPropertyFlags flags) {
    uint32_t type = 0;
    for (; type < memory.memoryTypeCount; ++type)
      if ((required.memoryTypeBits & (1u << type)) &&
          (memory.memoryTypes[type].propertyFlags & flags) == flags) break;
    Need(type < memory.memoryTypeCount, "no compatible Vulkan memory type");
    VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    info.allocationSize = required.size; info.memoryTypeIndex = type;
    VkDeviceMemory result{};
    Need(fn.vkAllocateMemory(vk, &info, nullptr, &result) == VK_SUCCESS, "memory allocation failed");
    cleanup.actions.emplace_back([&, result] { fn.vkFreeMemory(vk, result, nullptr); });
    return result;
  };
  glslang::InitializeProcess();
  const auto vertex = Compile(R"(#version 450
layout(push_constant) uniform Depth { float value; };
void main(){gl_Position=vec4(gl_VertexIndex==1?3:-1,gl_VertexIndex==2?3:-1,value,1);})", EShLangVertex);
  const auto pixel = Compile("#version 450\nvoid main(){}", EShLangFragment);
  glslang::FinalizeProcess();
  auto module = [&](const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size() * 4; info.pCode = code.data();
    VkShaderModule result{};
    Need(fn.vkCreateShaderModule(vk, &info, nullptr, &result) == VK_SUCCESS, "shader creation failed");
    cleanup.actions.emplace_back([&, result] { fn.vkDestroyShaderModule(vk, result, nullptr); });
    return result;
  };
  const auto vs = module(vertex), ps = module(pixel);
  const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 4};
  VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.pushConstantRangeCount = 1; layout_info.pPushConstantRanges = &push;
  VkPipelineLayout layout{};
  Need(fn.vkCreatePipelineLayout(vk, &layout_info, nullptr, &layout) == VK_SUCCESS, "pipeline layout");
  cleanup.actions.emplace_back([&, layout] { fn.vkDestroyPipelineLayout(vk, layout, nullptr); });
  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.queueFamilyIndex = device->queue_family_graphics_compute();
  VkCommandPool pool{};
  Need(fn.vkCreateCommandPool(vk, &pool_info, nullptr, &pool) == VK_SUCCESS, "command pool");
  cleanup.actions.emplace_back([&, pool] { fn.vkDestroyCommandPool(vk, pool, nullptr); });
  constexpr uint32_t extent = 16, output_size = extent * extent * 4;
  unsigned failures = 0, cases = 0;
  for (bool float24 : {true, false}) {
    const VkFormat format = float24 ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_D24_UNORM_S8_UINT;
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D; image_info.format = format;
    image_info.extent = {extent, extent, 1}; image_info.mipLevels = image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT; image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image{};
    Need(fn.vkCreateImage(vk, &image_info, nullptr, &image) == VK_SUCCESS, "depth image");
    VkMemoryRequirements required{};
    fn.vkGetImageMemoryRequirements(vk, image, &required);
    const auto image_memory = allocate(required, 0);
    Need(fn.vkBindImageMemory(vk, image, image_memory, 0) == VK_SUCCESS, "bind depth image");
    cleanup.actions.emplace_back([&, image] { fn.vkDestroyImage(vk, image, nullptr); });
    const VkImageAspectFlags aspects = VK_IMAGE_ASPECT_DEPTH_BIT |
        (float24 ? 0u : VK_IMAGE_ASPECT_STENCIL_BIT);
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image; view_info.viewType = VK_IMAGE_VIEW_TYPE_2D; view_info.format = format;
    view_info.subresourceRange = {aspects, 0, 1, 0, 1};
    VkImageView view{};
    Need(fn.vkCreateImageView(vk, &view_info, nullptr, &view) == VK_SUCCESS, "depth view");
    cleanup.actions.emplace_back([&, view] { fn.vkDestroyImageView(vk, view, nullptr); });
    VkAttachmentDescription attachment{};
    attachment.format = format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depth{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depth;
    VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass_info.attachmentCount = pass_info.subpassCount = 1;
    pass_info.pAttachments = &attachment; pass_info.pSubpasses = &subpass;
    VkRenderPass pass{};
    Need(fn.vkCreateRenderPass(vk, &pass_info, nullptr, &pass) == VK_SUCCESS, "render pass");
    cleanup.actions.emplace_back([&, pass] { fn.vkDestroyRenderPass(vk, pass, nullptr); });
    VkFramebufferCreateInfo framebuffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_info.renderPass = pass; framebuffer_info.attachmentCount = framebuffer_info.layers = 1;
    framebuffer_info.pAttachments = &view; framebuffer_info.width = framebuffer_info.height = extent;
    VkFramebuffer framebuffer{};
    Need(fn.vkCreateFramebuffer(vk, &framebuffer_info, nullptr, &framebuffer) == VK_SUCCESS, "framebuffer");
    cleanup.actions.emplace_back([&, framebuffer] { fn.vkDestroyFramebuffer(vk, framebuffer, nullptr); });
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = ps;
    VkPipelineVertexInputStateCreateInfo inputs{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount = viewport_state.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1; raster.depthBiasEnable = VK_TRUE;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth_state{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_state.depthTestEnable = depth_state.depthWriteEnable = VK_TRUE; depth_state.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    const VkDynamicState dynamic[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamics{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamics.dynamicStateCount = 3; dynamics.pDynamicStates = dynamic;
    VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount = 2; pipeline_info.pStages = stages; pipeline_info.pVertexInputState = &inputs;
    pipeline_info.pInputAssemblyState = &assembly; pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &raster; pipeline_info.pMultisampleState = &samples;
    pipeline_info.pDepthStencilState = &depth_state; pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDynamicState = &dynamics; pipeline_info.layout = layout; pipeline_info.renderPass = pass;
    VkPipeline pipeline{};
    Need(fn.vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) == VK_SUCCESS, "graphics pipeline");
    cleanup.actions.emplace_back([&, pipeline] { fn.vkDestroyPipeline(vk, pipeline, nullptr); });
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = output_size; buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer output{};
    Need(fn.vkCreateBuffer(vk, &buffer_info, nullptr, &output) == VK_SUCCESS, "readback buffer");
    fn.vkGetBufferMemoryRequirements(vk, output, &required);
    const auto output_memory = allocate(required, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Need(fn.vkBindBufferMemory(vk, output, output_memory, 0) == VK_SUCCESS, "bind readback buffer");
    cleanup.actions.emplace_back([&, output] { fn.vkDestroyBuffer(vk, output, nullptr); });
    void* mapping{};
    Need(fn.vkMapMemory(vk, output_memory, 0, VK_WHOLE_SIZE, 0, &mapping) == VK_SUCCESS, "map readback");
    cleanup.actions.emplace_back([&, output_memory] { fn.vkUnmapMemory(vk, output_memory); });
    for (bool reversed : {false, true}) for (float z : {0.125f, 0.75f, 0.9921875f})
      for (float offset : {-1.0f / 4096, 1.0f / 4096}) {
        RegisterFile regs{};
        reg::PA_SU_SC_MODE_CNTL mode{}; mode.poly_offset_front_enable = 1;
        regs[mode.register_index] = mode.value;
        reg::RB_DEPTH_INFO guest_depth{};
        guest_depth.depth_format = float24 ? xenos::DepthRenderTargetFormat::kD24FS8 : xenos::DepthRenderTargetFormat::kD24S8;
        regs[guest_depth.register_index] = guest_depth.value;
        regs[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET] = std::bit_cast<uint32_t>(offset);
        const auto bias = GetHostDepthBias(regs, true, 1, 1, properties, !legacy);
        VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command_info.commandPool = pool; command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_info.commandBufferCount = 1;
        VkCommandBuffer command{};
        Need(fn.vkAllocateCommandBuffers(vk, &command_info, &command) == VK_SUCCESS, "command buffer");
        VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Need(fn.vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS, "begin commands");
        VkClearValue clear{}; clear.depthStencil.depth = 1;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = pass; begin.framebuffer = framebuffer; begin.renderArea.extent = {extent, extent};
        begin.clearValueCount = 1; begin.pClearValues = &clear;
        fn.vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        const float host_range = float24 ? 0.5f : 1.0f;
        VkViewport viewport{0, 0, float(extent), float(extent), reversed ? host_range : 0.0f, reversed ? 0.0f : host_range};
        VkRect2D scissor{{0, 0}, {extent, extent}};
        fn.vkCmdSetViewport(command, 0, 1, &viewport); fn.vkCmdSetScissor(command, 0, 1, &scissor);
        fn.vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        fn.vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 4, &z);
        DriverCommands commands{fn, command};
        RecordHostDepthBias(commands, bias);
        Need(commands.constant_calls == unsigned(!legacy && !fallback) &&
             commands.legacy_calls == unsigned(legacy || fallback), "wrong Vulkan command selected");
        fn.vkCmdDraw(command, 3, 1, 0, 0); fn.vkCmdEndRenderPass(command);
        VkImageMemoryBarrier image_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        image_barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = image; image_barrier.subresourceRange = {aspects, 0, 1, 0, 1};
        fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        copy.imageExtent = {extent, extent, 1};
        fn.vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output, 1, &copy);
        VkMemoryBarrier host_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host_barrier, 0, nullptr, 0, nullptr);
        Need(fn.vkEndCommandBuffer(command) == VK_SUCCESS, "end commands");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence{};
        Need(fn.vkCreateFence(vk, &fence_info, nullptr, &fence) == VK_SUCCESS, "fence");
        cleanup.actions.emplace_back([&, fence] { fn.vkDestroyFence(vk, fence, nullptr); });
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        { auto queue = device->AcquireQueue(pool_info.queueFamilyIndex, 0);
          Need(fn.vkQueueSubmit(queue.queue(), 1, &submit, fence) == VK_SUCCESS, "submit"); }
        Need(fn.vkWaitForFences(vk, 1, &fence, VK_TRUE, 5'000'000'000ull) == VK_SUCCESS, "GPU timeout");
        const float expected = (reversed ? 1 - z : z) * host_range + offset * host_range;
        float maximum_error = 0, first_value = 0;
        for (unsigned i = 0; i < extent * extent; ++i) {
          const auto word = static_cast<const uint32_t*>(mapping)[i];
          const float actual = float24 ? std::bit_cast<float>(word) : float(word & 0xFFFFFFu) / 16777215.0f;
          Need(std::isfinite(actual) && actual >= 0 && actual <= 1, "invalid depth readback");
          if (!i) first_value = actual;
          maximum_error = std::max(maximum_error, std::abs(actual - expected));
        }
        const bool passed = maximum_error <= 2.0f / 16777215.0f;
        failures += !passed; ++cases;
        std::cout << "float24=" << float24 << " reversed=" << reversed << " guest_z=" << z
                  << " offset=" << offset << " depth=" << first_value << " expected=" << expected
                  << " max_error=" << maximum_error << " exact=" << passed << '\n';
      }
  }
  std::cout << "cases=" << cases << " exact_failures=" << failures << '\n';
  if (!fallback) Need(failures == 0, "host depth bias shrank with primitive depth");
  std::cout << (fallback ? "PASS optional-feature fallback command/resource path\n" : "PASS constant guest depth bias on real GPU\n");
}
}
int main(int argc, char** argv) {
  try {
    const std::string_view mode = argc > 1 ? argv[1] : "correct";
    Need(mode == "correct" || mode == "legacy" || mode == "fallback", "use correct, legacy or fallback");
    Run(mode == "legacy", mode == "fallback");
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
