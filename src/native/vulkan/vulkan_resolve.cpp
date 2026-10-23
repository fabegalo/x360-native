#include "vulkan_render.h"
#include "../commands/resolve_coverage.h"
#include <stdexcept>

namespace x360n::native {
using namespace rex::graphics;
namespace kernels {
#include "src/graphics/shaders/vulkan_spirv/resolve_fast_32bpp_1x2xmsaa_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fast_32bpp_4xmsaa_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fast_64bpp_1x2xmsaa_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fast_64bpp_4xmsaa_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_8bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_16bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_32bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_64bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_128bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_clear_32bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_clear_64bpp_cs.h"
}
void VulkanRender::Resolve(const RegisterFile& regs, rex::memory::Memory& memory) {
  auto& mirror = Mirror(memory);
  const auto fetch = regs.GetVertexFetch(0);
  if (fetch.type == xenos::FetchConstantType::kVertex && fetch.size == 6 &&
      uint64_t(fetch.address) * 4 + 24 <= GuestMemoryMirror::kSize) {
    SyncGuestRead(fetch.address << 2, 24, 0xFF);
    float vertices[6];
    const auto* guest = memory.TranslatePhysical<const float*>(fetch.address << 2);
    for (unsigned i = 0; i < 6; ++i) vertices[i] = xenos::GpuSwap(guest[i], fetch.endian);
    if (ResolveCoverageEmpty(regs, vertices)) return;
  }
  draw_util::ResolveInfo info;
  if (!draw_util::GetResolveInfo(regs, memory, 1, 1, false, false, info))
    throw std::runtime_error("Native Vulkan: invalid resolve registers or vertices");
  const auto length = info.copy_dest_extent_length;
  if (uint64_t(info.copy_dest_extent_start) + length > GuestMemoryMirror::kSize)
    throw std::runtime_error("Native Vulkan: resolve outside guest physical memory");
  Begin();
  if (arenas_[submissions_.current_slot()].descriptor_count > 4000 ||
      readback_used_ + length > readback_->size()) Flush(false);
  if (length > readback_->size())
    readback_ = std::make_unique<VulkanBuffer>(device_, length, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              VulkanBuffer::Access::kReadback);
  Begin();
  EndRendering();
  const auto& fn = device_.functions();
  const auto edram_set = AllocateDescriptors(storage_layout_);
  const VkDescriptorBufferInfo edram_info{edram_buffer_->handle(), 0, edram_buffer_->size()};
  VkWriteDescriptorSet edram_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  edram_write.dstSet = edram_set;
  edram_write.descriptorCount = 1;
  edram_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  edram_write.pBufferInfo = &edram_info;
  fn.vkUpdateDescriptorSets(device_.device(), 1, &edram_write, 0, nullptr);
  if (length) {
    draw_util::ResolveCopyShaderConstants constants;
    uint32_t groups_x, groups_y;
    const auto shader = info.GetCopyShader(1, 1, constants, groups_x, groups_y);
    if (shader == draw_util::ResolveCopyShaderIndex::kUnknown)
      throw std::runtime_error("Native Vulkan: unsupported resolve conversion");
    const auto index = size_t(shader);
    if (!copy_pipelines_[index]) {
      using namespace kernels;
      const std::span<const uint32_t> programs[]{resolve_fast_32bpp_1x2xmsaa_cs,
          resolve_fast_32bpp_4xmsaa_cs, resolve_fast_64bpp_1x2xmsaa_cs,
          resolve_fast_64bpp_4xmsaa_cs, resolve_full_8bpp_cs, resolve_full_16bpp_cs,
          resolve_full_32bpp_cs, resolve_full_64bpp_cs, resolve_full_128bpp_cs};
      copy_pipelines_[index] = ComputePipeline(copy_layout_, programs[index]);
    }
    // Partial tiles and the padding around the copied rectangle retain their
    // latest bytes. GPU resolve output stays resident for later GPU readers.
    mirror.RequestRange(info.copy_dest_extent_start, length);
    const auto alignment = device_.properties().minStorageBufferOffsetAlignment;
    const uint64_t base = (uint64_t(info.copy_dest_base) / alignment) * alignment;
    const uint64_t range = uint64_t(info.copy_dest_extent_start) + length - base;
    if (range > device_.properties().maxStorageBufferRange)
      throw std::runtime_error("Native Vulkan: resolve destination exceeds storage buffer range");
    const VkDescriptorBufferInfo destination{guest_buffer_->handle(), base, range};
    const auto dest_set = AllocateDescriptors(storage_layout_);
    auto dest_write = edram_write;
    dest_write.dstSet = dest_set;
    dest_write.pBufferInfo = &destination;
    fn.vkUpdateDescriptorSets(device_.device(), 1, &dest_write, 0, nullptr);
    UseBuffer(guest_buffer_->handle(), guest_access_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    fn.vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, copy_pipelines_[index]);
    const VkDescriptorSet sets[]{edram_set, dest_set};
    fn.vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, copy_layout_, 0, 2, sets, 0, nullptr);
    constants.dest_base -= uint32_t(base);
    fn.vkCmdPushConstants(commands_, copy_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    fn.vkCmdDispatch(commands_, groups_x, groups_y, 1);
    std::vector<uint64_t> cpu_generations;
    mirror.RangeWrittenByGpu(info.copy_dest_extent_start, length, &cpu_generations);
    UseBuffer(guest_buffer_->handle(), guest_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    const VkBufferCopy copy{info.copy_dest_extent_start, readback_used_, length};
    fn.vkCmdCopyBuffer(commands_, guest_buffer_->handle(), readback_->handle(), 1, &copy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = readback_->handle();
    host.offset = readback_used_;
    host.size = length;
    fn.vkCmdPipelineBarrier(commands_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &host, 0, nullptr);
    writeback_.Add(info.copy_dest_extent_start, length, readback_used_, std::move(cpu_generations));
    readback_used_ += length;
  }
  const auto groups = info.GetClearShaderGroupCount(1, 1);
  auto clear = [&](bool depth) {
    const unsigned index = !depth && info.color_edram_info.format_is_64bpp;
    if (!clear_pipelines_[index])
      clear_pipelines_[index] = ComputePipeline(clear_layout_, index
          ? std::span<const uint32_t>(kernels::resolve_clear_64bpp_cs)
          : std::span<const uint32_t>(kernels::resolve_clear_32bpp_cs));
    draw_util::ResolveClearShaderConstants constants;
    if (depth) info.GetDepthClearShaderConstants(constants); else info.GetColorClearShaderConstants(constants);
    // Preserve depth/color clear ordering even when their EDRAM regions alias.
    UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
               VK_ACCESS_SHADER_WRITE_BIT);
    fn.vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, clear_pipelines_[index]);
    fn.vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, clear_layout_, 0, 1, &edram_set, 0, nullptr);
    fn.vkCmdPushConstants(commands_, clear_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    fn.vkCmdDispatch(commands_, groups.first, groups.second, 1);
  };
  if (info.IsClearingDepth()) clear(true);
  if (info.IsClearingColor()) clear(false);
}
}
