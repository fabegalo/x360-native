#include "vulkan_render.h"
#include "../commands/resolve_coverage.h"
#include <stdexcept>
#include <bit>
#include <rex/graphics/pipeline/texture/info.h>
#include "../shaders/resolve_downsample.h"

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
#include "src/graphics/shaders/vulkan_spirv/resolve_fast_32bpp_1x2xmsaa_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fast_32bpp_4xmsaa_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fast_64bpp_1x2xmsaa_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fast_64bpp_4xmsaa_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_8bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_16bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_32bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_64bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_128bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_clear_32bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_clear_64bpp_scaled_cs.h"
}
void VulkanRender::Resolve(const RegisterFile& regs, rex::memory::Memory& memory) {
  SubmitIfGpuIdle();
  recorded_work_ += eager_threshold_;
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
  if (!draw_util::GetResolveInfo(regs, memory, scale_x_, scale_y_, false, false, info))
    throw std::runtime_error("Native Vulkan: invalid resolve registers or vertices");
  const auto length = info.copy_dest_extent_length;
  const bool scaled=scaled_.enabled(); const uint32_t area=scale_x_*scale_y_;
  const uint64_t scaled_range=uint64_t(info.copy_dest_extent_start)-info.copy_dest_base+length;

  if (uint64_t(info.copy_dest_extent_start) + length > GuestMemoryMirror::kSize)
    throw std::runtime_error("Native Vulkan: resolve outside guest physical memory");
  if (scaled && length) {
    const uint32_t begin=info.copy_dest_base&~4095u;
    const uint64_t end=(uint64_t(info.copy_dest_extent_start)+length+4095)&~uint64_t(4095);
    SyncGuestRead(begin,uint32_t(end-begin),0xFF);
  }
  Begin();
  if (arenas_[submissions_.current_slot()].descriptor_count > 4000 ||
      readback_used_ + length > readback_->size()) Flush(false);
  ReserveReadback(length);
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
    const auto shader = info.GetCopyShader(scale_x_, scale_y_, constants, groups_x, groups_y);
    if (shader == draw_util::ResolveCopyShaderIndex::kUnknown)
      throw std::runtime_error("Native Vulkan: unsupported resolve conversion");
    const auto index = size_t(shader);
    if (!copy_pipelines_[index].valid()) {
      using namespace kernels;
      const std::span<const uint32_t> programs[]{resolve_fast_32bpp_1x2xmsaa_cs,
          resolve_fast_32bpp_4xmsaa_cs, resolve_fast_64bpp_1x2xmsaa_cs,
          resolve_fast_64bpp_4xmsaa_cs, resolve_full_8bpp_cs, resolve_full_16bpp_cs,
          resolve_full_32bpp_cs, resolve_full_64bpp_cs, resolve_full_128bpp_cs};
      const std::span<const uint32_t> scaled_programs[]{resolve_fast_32bpp_1x2xmsaa_scaled_cs,
          resolve_fast_32bpp_4xmsaa_scaled_cs, resolve_fast_64bpp_1x2xmsaa_scaled_cs,
          resolve_fast_64bpp_4xmsaa_scaled_cs, resolve_full_8bpp_scaled_cs, resolve_full_16bpp_scaled_cs,
          resolve_full_32bpp_scaled_cs, resolve_full_64bpp_scaled_cs, resolve_full_128bpp_scaled_cs};
      copy_pipelines_[index] = ComputePipeline(copy_layout_, scaled ? scaled_programs[index] : programs[index]);
    }
    // Partial tiles and the padding around the copied rectangle retain their
    // latest bytes. GPU resolve output stays resident for later GPU readers.
    mirror.RequestRange(info.copy_dest_extent_start, length);
    const auto alignment = device_.properties().minStorageBufferOffsetAlignment;
    const uint64_t base = (uint64_t(info.copy_dest_base) / alignment) * alignment;
    const uint64_t range = uint64_t(info.copy_dest_extent_start) + length - base;
    if (range > device_.properties().maxStorageBufferRange)
      throw std::runtime_error("Native Vulkan: resolve destination exceeds storage buffer range");
    auto ticket=scaled?scaled_.Capture(info.copy_dest_extent_start,length):ScaledResolveCache::Ticket{};
    const unsigned pixel_log2=info.IsCopyingDepth()?2u:std::countr_zero(unsigned(FormatInfo::Get(xenos::TextureFormat(info.copy_dest_info.copy_dest_format))->bits_per_pixel/8));
    std::shared_ptr<VulkanBuffer> scaled_buffer,scaled_readback;
    BufferAccess scaled_access;
    if (scaled) {
      if (scaled_range*area>device_.properties().maxStorageBufferRange || scaled_range*area>UINT32_MAX)
        throw std::runtime_error("Native scaled resolve exceeds Vulkan storage buffer range");
      auto initial=scaled_.Hydrate(info.copy_dest_base,uint32_t(scaled_range),pixel_log2,memory);
      scaled_buffer=std::make_shared<VulkanBuffer>(device_,initial.size(),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,VulkanBuffer::Access::kDevice);
      scaled_readback=std::make_shared<VulkanBuffer>(device_,initial.size(),VK_BUFFER_USAGE_TRANSFER_DST_BIT,VulkanBuffer::Access::kReadback);
      const auto upload=Upload(initial.data(),initial.size(),4);
      UseBuffer(scaled_buffer->handle(),scaled_access,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
      const VkBufferCopy initial_copy{upload.offset,0,initial.size()};
      cmd_.CopyBuffer(upload.buffer,scaled_buffer->handle(),1,&initial_copy);
      UseBuffer(scaled_buffer->handle(),scaled_access,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
      submissions_.Retain(scaled_buffer); submissions_.Retain(scaled_readback);
    }
    const VkDescriptorBufferInfo destination{scaled ? scaled_buffer->handle() : guest_buffer_->handle(), scaled ? 0 : base, scaled ? scaled_range*area : range};
    const auto dest_set = AllocateDescriptors(storage_layout_);
    auto dest_write = edram_write;
    dest_write.dstSet = dest_set;
    dest_write.pBufferInfo = &destination;
    fn.vkUpdateDescriptorSets(device_.device(), 1, &dest_write, 0, nullptr);
    UseBuffer(guest_buffer_->handle(), guest_access_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    cmd_.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, copy_pipelines_[index]);
    const VkDescriptorSet sets[]{edram_set, dest_set};
    cmd_.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, copy_layout_, 0, 2, sets, 0, nullptr);
    if (!scaled) constants.dest_base -= uint32_t(base);
    cmd_.PushConstants(copy_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    cmd_.Dispatch(groups_x, groups_y, 1);
    if (scaled) {
      if (!downscale_pipeline_.valid()) downscale_pipeline_=ComputePipeline(downscale_layout_,CompileNativeGlsl(VK_SHADER_STAGE_COMPUTE_BIT,kResolveDownsampleGlsl));
      UseBuffer(scaled_buffer->handle(),scaled_access,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
      const auto source_set=AllocateDescriptors(storage_layout_), canonical_set=AllocateDescriptors(storage_layout_);
      const VkDescriptorBufferInfo source{scaled_buffer->handle(),0,scaled_range*area}, canonical{guest_buffer_->handle(),base,range};
      VkWriteDescriptorSet writes[]{edram_write,edram_write}; writes[0].dstSet=source_set; writes[0].pBufferInfo=&source;
      writes[1].dstSet=canonical_set; writes[1].pBufferInfo=&canonical;
      fn.vkUpdateDescriptorSets(device_.device(),2,writes,0,nullptr);
      UseBuffer(guest_buffer_->handle(),guest_access_,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
      const uint32_t downsample[]{length,pixel_log2,scale_x_,scale_y_,(info.copy_dest_extent_start-info.copy_dest_base)*area,uint32_t(info.copy_dest_extent_start-base)};
      const VkDescriptorSet downscale_sets[]{source_set,canonical_set};
      cmd_.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,downscale_pipeline_);
      cmd_.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,downscale_layout_,0,2,downscale_sets,0,nullptr);
      cmd_.PushConstants(downscale_layout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(downsample),downsample);
      cmd_.Dispatch((length+255)/256,1,1);
      UseBuffer(scaled_buffer->handle(),scaled_access,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
      const VkBufferCopy scaled_copy{0,0,scaled_range*area};
      cmd_.CopyBuffer(scaled_buffer->handle(),scaled_readback->handle(),1,&scaled_copy);
      VkBufferMemoryBarrier ready{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
      ready.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; ready.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
      ready.srcQueueFamilyIndex=ready.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
      ready.buffer=scaled_readback->handle(); ready.size=VK_WHOLE_SIZE;
      cmd_.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&ready,0,nullptr);
    }
    std::vector<uint64_t> cpu_generations;
    mirror.RangeWrittenByGpu(info.copy_dest_extent_start, length, &cpu_generations, ticket.cpu_generations);
    if (scaled) {
      scaled_.Begin(ticket);
      scaled_readbacks_.push_back({std::move(ticket),scaled_readback,pixel_log2,uint64_t(info.copy_dest_extent_start-info.copy_dest_base)*area});
    }
    UseBuffer(guest_buffer_->handle(), guest_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    const VkBufferCopy copy{info.copy_dest_extent_start, readback_used_, length};
    cmd_.CopyBuffer(guest_buffer_->handle(), readback_->handle(), 1, &copy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = readback_->handle();
    host.offset = readback_used_;
    host.size = length;
    cmd_.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &host, 0, nullptr);
    writeback_.Add(info.copy_dest_extent_start, length, readback_used_, std::move(cpu_generations));
    readback_used_ += length;
  }
  const auto groups = info.GetClearShaderGroupCount(scale_x_, scale_y_);
  auto clear = [&](bool depth) {
    const unsigned index = !depth && info.color_edram_info.format_is_64bpp;
    if (!clear_pipelines_[index].valid())
      clear_pipelines_[index] = ComputePipeline(clear_layout_, scaled ? (index ? std::span<const uint32_t>(kernels::resolve_clear_64bpp_scaled_cs)
          : std::span<const uint32_t>(kernels::resolve_clear_32bpp_scaled_cs)) : index
          ? std::span<const uint32_t>(kernels::resolve_clear_64bpp_cs)
          : std::span<const uint32_t>(kernels::resolve_clear_32bpp_cs));
    draw_util::ResolveClearShaderConstants constants;
    if (depth) info.GetDepthClearShaderConstants(constants); else info.GetColorClearShaderConstants(constants);
    // Preserve depth/color clear ordering even when their EDRAM regions alias.
    UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
               VK_ACCESS_SHADER_WRITE_BIT);
    cmd_.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, clear_pipelines_[index]);
    cmd_.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, clear_layout_, 0, 1, &edram_set, 0, nullptr);
    cmd_.PushConstants(clear_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    cmd_.Dispatch(groups.first, groups.second, 1);
  };
  if (info.IsClearingDepth()) clear(true);
  if (info.IsClearingColor()) clear(false);
}
}
