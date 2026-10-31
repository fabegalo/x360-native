#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace x360n::native {
struct VulkanStorageBufferRange { VkDeviceSize offset, size; };

// Keep descriptors reusable across adjacent loads, but allow a dispatch to
// cross a window boundary. The shader receives byte offsets relative to this
// descriptor; alignment must include both the device and shader element size.
inline VulkanStorageBufferRange TextureStorageBufferRange(VkDeviceSize buffer_size,
    VkDeviceSize offset, VkDeviceSize length, VkDeviceSize limit, VkDeviceSize alignment) {
  assert(alignment); // Device alignment combined with the shader's 8/16-byte element.
  if (!length || offset >= buffer_size || length > buffer_size - offset)
    throw std::runtime_error("Native Vulkan: texture load dispatch exceeds its buffer");
  if (buffer_size <= limit) return {0, buffer_size};
  const VkDeviceSize window = limit / alignment * alignment;
  if (!window) throw std::runtime_error("Native Vulkan: texture load storage alignment exceeds maxStorageBufferRange");
  VkDeviceSize base = offset / window * window;
  if (offset - base + length > limit) base = offset / alignment * alignment;
  const VkDeviceSize size = std::min(limit, buffer_size - base);
  if (offset - base + length > size)
    throw std::runtime_error("Native Vulkan: texture load dispatch exceeds maxStorageBufferRange");
  return {base, size};
}
}
