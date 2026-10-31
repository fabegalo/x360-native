#pragma once
#include <rex/ui/vulkan/device.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>

namespace x360n::native {
// Command-thread diagnostics. No clocks, atomics or registry lookups in the
// disabled hot path. Enable before creating the backend.
struct VulkanPerformance {
  bool enabled = false;
  uint64_t submissions = 0, waits = 0, blocked_waits = 0, drains = 0;
  uint64_t submit_ns = 0, wait_ns = 0, readback_ns = 0, guest_sync_ns = 0;
  uint64_t readbacks = 0, readback_bytes = 0, guest_syncs = 0;
  uint64_t buffers = 0, allocated_bytes = 0, live_bytes = 0, peak_bytes = 0;
  uint64_t gpu_ns = 0, gpu_samples = 0;
  uint64_t presents = 0, draws = 0, draw_ns = 0, indices_ns = 0;
  uint64_t gpu_index_draws = 0, gpu_index_bytes = 0, normalized_indices = 0;
};
VulkanPerformance& NativeVulkanPerformance();
class VulkanPerformanceTimer {
 public:
  explicit VulkanPerformanceTimer(uint64_t& total)
      : total_(NativeVulkanPerformance().enabled ? &total : nullptr) {
    if (total_) start_ = std::chrono::steady_clock::now();
  }
  ~VulkanPerformanceTimer() {
    if (total_) *total_ += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start_).count());
  }
 private:
  uint64_t* total_;
  std::chrono::steady_clock::time_point start_;
};
void CheckVulkan(VkResult result, const char* operation);
// Core Vulkan entry points unused by the provider need not be added to the
// SDK's executor function table. Native loads them from its own device.
template<class Function>
Function LoadNativeVulkanFunction(const rex::ui::vulkan::VulkanDevice& device, const char* name) {
  auto function = reinterpret_cast<Function>(
      device.vulkan_instance()->functions().vkGetDeviceProcAddr(device.device(), name));
  if (!function) throw std::runtime_error(std::string("Native Vulkan: missing device function ") + name);
  return function;
}

// Native owns these buffers, command pools and fences. Only the runtime's
// device/queue provider is shared; no Xenos executor or resource cache is used.
class VulkanBuffer {
 public:
  enum class Access { kDevice, kUpload, kReadback };
  VulkanBuffer(const rex::ui::vulkan::VulkanDevice&, VkDeviceSize size,
               VkBufferUsageFlags usage, Access);
  ~VulkanBuffer();
  VulkanBuffer(const VulkanBuffer&) = delete;
  VulkanBuffer& operator=(const VulkanBuffer&) = delete;
  VkBuffer handle() const { return buffer_; }
  VkDeviceSize size() const { return size_; }
  std::byte* mapped() const { return mapped_; }
  // The caller owns the range until its submission completes. Non-coherent
  // allocation atoms are included in flush/invalidate without leaving memory.
  void Flush(VkDeviceSize offset, VkDeviceSize size);
  void Invalidate(VkDeviceSize offset, VkDeviceSize size);
 private:
  void SynchronizeMapping(VkDeviceSize offset, VkDeviceSize size, bool invalidate);
  const rex::ui::vulkan::VulkanDevice& device_;
  VkBuffer buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
  VkDeviceSize size_, allocation_size_ = 0;
  std::byte* mapped_ = nullptr;
  bool coherent_ = false;
  bool measured_ = false;
};

// A bounded ring of independent submissions. Begin waits only when reusing a
// slot; all resources retained there survive until its real GPU fence signals.
// All recording/retention calls belong to the command processor thread.
class VulkanSubmissionRing {
 public:
  static constexpr size_t kSlots = 3;
  explicit VulkanSubmissionRing(const rex::ui::vulkan::VulkanDevice&);
  ~VulkanSubmissionRing();
  VulkanSubmissionRing(const VulkanSubmissionRing&) = delete;
  VulkanSubmissionRing& operator=(const VulkanSubmissionRing&) = delete;
  VkCommandBuffer Begin();
  uint64_t Submit();
  void Wait(uint64_t serial);
  // Observe signaled fences without blocking. Queue order makes every earlier
  // submission complete too; a reused slot has already retired its old serial.
  void Poll();
  void Drain();
  size_t current_slot() const { return size_t(serial_ % kSlots); }
  uint64_t last_submitted() const { return serial_; }
  uint64_t completed() const { return completed_; }
  template<class T> void Retain(std::shared_ptr<T> resource) {
    RetainResource(std::move(resource));
  }
 private:
  void RetainResource(std::shared_ptr<void>);
  void Destroy();
  void MeasureCompleted();
  struct Slot {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint64_t serial = 0;
    bool timed = false;
    std::vector<std::shared_ptr<void>> retained;
  };
  const rex::ui::vulkan::VulkanDevice& device_;
  std::array<Slot, kSlots> slots_;
  uint64_t serial_ = 0, completed_ = 0;
  bool recording_ = false;
  VkQueryPool timestamps_ = VK_NULL_HANDLE;
  unsigned timestamp_bits_ = 0;
  float timestamp_period_ = 0;
};

// Host shaders only. Guest shaders go through the shared PowerPC GPU shader
// semantics in SpirvShaderTranslator and the Native-owned shader cache.
std::vector<uint32_t> CompileNativeGlsl(VkShaderStageFlagBits stage, std::string_view source);
VkShaderModule CreateNativeShaderModule(const rex::ui::vulkan::VulkanDevice&,
                                        std::span<const uint32_t> spirv);
}
