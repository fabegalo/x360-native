// GPU-resident scaled resolve memory. One buffer covers guest address * scale
// area; with sparse residency, memory is bound on first use in granules as in
// the pinned ReXGlue VulkanTextureCache (Xenia, BSD-3-Clause).
#include "vulkan_render.h"
#include "../shaders/scaled_memory.h"
#include <algorithm>
#include <bit>
#include <stdexcept>
namespace x360n::native {
void VulkanRender::DestroyScaled() {
  const auto& fn = device_.functions();
  if (scaled_buffer_) fn.vkDestroyBuffer(device_.device(), scaled_buffer_, nullptr);
  for (auto memory : scaled_memory_) fn.vkFreeMemory(device_.device(), memory, nullptr);
  scaled_buffer_ = VK_NULL_HANDLE; scaled_memory_.clear(); scaled_bound_.clear();
  retired_scanout_buffers_.clear(); scanout_buffer_.reset();
}
VkDescriptorSet VulkanRender::StorageSet(const VkDescriptorBufferInfo& info) {
  const auto set = AllocateDescriptors(storage_layout_);
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write.pBufferInfo = &info;
  device_.functions().vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
  return set;
}
VkDescriptorBufferInfo VulkanRender::UseScaled(uint32_t start, uint64_t length, VkPipelineStageFlags stage, VkAccessFlags access) {
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  const uint64_t area = uint64_t(scale_x_) * scale_y_, total = uint64_t(GuestMemoryMirror::kSize) * area;
  const uint64_t first = uint64_t(start) * area, size = length * area;
  const auto& properties = device_.properties();
  if (!length || first + size > total || size > properties.maxStorageBufferRange || first % properties.minStorageBufferOffsetAlignment)
    throw std::runtime_error("Native Vulkan: scaled memory range exceeds storage buffer limits");
  if (!scaled_buffer_) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = total; info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scaled_sparse_ = properties.sparseBinding && properties.sparseResidencyBuffer &&
        device_.queue_family_sparse_binding() != UINT32_MAX;
    if (scaled_sparse_) info.flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT;
    CheckVulkan(fn.vkCreateBuffer(vk, &info, nullptr, &scaled_buffer_), "create scaled resolve buffer");
    VkMemoryRequirements requirements;
    fn.vkGetBufferMemoryRequirements(vk, scaled_buffer_, &requirements);
    auto types = requirements.memoryTypeBits & device_.memory_types().device_local;
    if (!types) throw std::runtime_error("Native Vulkan: no device-local memory for scaled resolves");
    scaled_memory_type_ = std::countr_zero(types);
    if (scaled_sparse_) {
      const uint32_t size_log2 = 64 - std::countl_zero(total - 1);
      scaled_granularity_log2_ = std::min(size_log2, std::max<uint32_t>(std::countr_zero(std::max<VkDeviceSize>(requirements.alignment, 1)),
          std::max<uint32_t>(22, size_log2 > 10 ? size_log2 - 10 : 0)));
      scaled_bound_.assign(size_t(((total - 1) >> scaled_granularity_log2_) + 1), false);
    } else {
      // Without sparse residency the whole address space is device memory.
      VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocate.allocationSize = requirements.size; allocate.memoryTypeIndex = scaled_memory_type_;
      VkDeviceMemory memory;
      CheckVulkan(fn.vkAllocateMemory(vk, &allocate, nullptr, &memory), "allocate scaled resolve memory");
      scaled_memory_.push_back(memory);
      CheckVulkan(fn.vkBindBufferMemory(vk, scaled_buffer_, memory, 0), "bind scaled resolve memory");
    }
  }
  if (scaled_sparse_) {
    const size_t last = size_t((first + size - 1) >> scaled_granularity_log2_);
    for (size_t granule = size_t(first >> scaled_granularity_log2_); granule <= last;) {
      if (scaled_bound_[granule]) { ++granule; continue; }
      size_t end = granule;
      while (end <= last && !scaled_bound_[end]) ++end;
      VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocate.allocationSize = VkDeviceSize(end - granule) << scaled_granularity_log2_;
      allocate.memoryTypeIndex = scaled_memory_type_;
      VkDeviceMemory memory;
      CheckVulkan(fn.vkAllocateMemory(vk, &allocate, nullptr, &memory), "allocate scaled resolve granule");
      scaled_memory_.push_back(memory);
      VkSparseMemoryBind bind{VkDeviceSize(granule) << scaled_granularity_log2_, allocate.allocationSize, memory, 0, 0};
      VkSparseBufferMemoryBindInfo buffer_bind{scaled_buffer_, 1, &bind};
      VkBindSparseInfo info{VK_STRUCTURE_TYPE_BIND_SPARSE_INFO};
      info.bufferBindCount = 1; info.pBufferBinds = &buffer_bind;
      // New granules have no earlier user. Waiting here (first use only)
      // completes the binding before the command buffer being recorded runs.
      VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      VkFence fence;
      CheckVulkan(fn.vkCreateFence(vk, &fence_info, nullptr, &fence), "create sparse bind fence");
      VkResult result;
      {
        auto queue = device_.AcquireQueue(device_.queue_family_sparse_binding(), 0);
        result = fn.vkQueueBindSparse(queue.queue(), 1, &info, fence);
      }
      if (result == VK_SUCCESS) result = fn.vkWaitForFences(vk, 1, &fence, VK_TRUE, UINT64_MAX);
      fn.vkDestroyFence(vk, fence, nullptr);
      CheckVulkan(result, "bind scaled resolve memory");
      std::fill(scaled_bound_.begin() + granule, scaled_bound_.begin() + end, true);
      granule = end;
    }
  }
  UseBuffer(scaled_buffer_, scaled_access_, stage, access);
  return {scaled_buffer_, first, size};
}
void VulkanRender::ReplicateScaled(uint32_t start, uint32_t length, unsigned pixel_log2, bool mark) {
  const auto runs = scaled_.Stale(start, length);
  if (runs.empty()) return;
  if (!replicate_pipeline_.valid())
    replicate_pipeline_ = ComputePipeline(downscale_layout_, CompileNativeGlsl(VK_SHADER_STAGE_COMPUTE_BIT, kScaledReplicateGlsl));
  const uint32_t area = scale_x_ * scale_y_;
  for (auto [run_start, run_length] : runs) {
    mirror_->RequestRange(run_start, run_length);
    Begin(); EndRendering();
    UseBuffer(guest_buffer_->handle(), guest_access_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    const auto destination = UseScaled(run_start, run_length, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    const VkDescriptorSet sets[]{StorageSet({guest_buffer_->handle(), run_start, run_length}), StorageSet(destination)};
    cmd_.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, replicate_pipeline_);
    cmd_.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, downscale_layout_, 0, 2, sets, 0, nullptr);
    // 1 MB guest chunks bound each dispatch's group count.
    for (uint32_t done = 0; done < run_length; done += 1u << 20) {
      const uint32_t chunk = std::min(run_length - done, 1u << 20);
      const uint32_t constants[]{chunk * area, pixel_log2, scale_x_, scale_y_, done, done * area};
      cmd_.PushConstants(downscale_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);
      cmd_.Dispatch((chunk * area / 4 + 63) / 64, 1, 1);
    }
    if (mark) scaled_.MarkCurrent(run_start, run_length);
  }
}
VkBuffer VulkanRender::PrepareScanout(const ScanoutTexture& input, ScanoutTexture& output) {
  if (!mirror_ || !scaled_.Overlaps(input.address, input.bytes)) return VK_NULL_HANDLE;
  const uint64_t width = uint64_t(input.width) * scale_x_, height = uint64_t(input.height) * scale_y_;
  const uint64_t pitch = (width + 31) & ~uint64_t(31), size = pitch * height * 4;
  if (!width || !height || size > device_.properties().maxStorageBufferRange || width > 16384 || height > 16384 || (input.address & 15))
    throw std::runtime_error("Native: requested scale exceeds scanout texture limits");
  output = input; output.width = uint32_t(width); output.height = uint32_t(height); output.pitch = uint32_t(pitch);
  output.tiled = 0; output.bytes = uint32_t(size);
  ReplicateScaled(input.address, input.bytes, 2, true);
  if (!scaled_scanout_pipeline_.valid())
    scaled_scanout_pipeline_ = ComputePipeline(downscale_layout_, CompileNativeGlsl(VK_SHADER_STAGE_COMPUTE_BIT, kScaledScanoutGlsl));
  if (!scanout_buffer_ || scanout_buffer_->size() < size) {
    // The scanout's own submissions may still read an earlier one: keep it.
    if (scanout_buffer_) retired_scanout_buffers_.push_back(std::move(scanout_buffer_));
    scanout_buffer_ = std::make_shared<VulkanBuffer>(device_, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VulkanBuffer::Access::kDevice);
    scanout_access_ = {};
  }
  Begin(); EndRendering();
  const auto source = UseScaled(input.address, input.bytes, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
  // Orders the previous frame's scanout reads (same queue) before this write.
  UseBuffer(scanout_buffer_->handle(), scanout_access_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
  const VkDescriptorSet sets[]{StorageSet(source), StorageSet({scanout_buffer_->handle(), 0, size})};
  cmd_.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, scaled_scanout_pipeline_);
  cmd_.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, downscale_layout_, 0, 2, sets, 0, nullptr);
  const uint32_t constants[]{output.width, output.height, output.pitch, input.pitch, input.tiled, scale_x_, scale_y_, 0};
  cmd_.PushConstants(downscale_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);
  cmd_.Dispatch((output.width + 7) / 8, (output.height + 7) / 8, 1);
  // Visible to the scanout's fragment shader, submitted next on this queue.
  UseBuffer(scanout_buffer_->handle(), scanout_access_, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
  submissions_.Retain(scanout_buffer_);
  return scanout_buffer_->handle();
}
}
