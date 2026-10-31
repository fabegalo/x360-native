#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <bit>
#include <optional>

namespace x360n::native {
// A buffer can have several simultaneous readers after its last writer. Keep
// the writer until each new read scope has a memory dependency from it: a
// write made visible to transfer reads is not yet visible to shader reads.
// Actual reader scopes are retained for the next write-after-read dependency.
class VulkanBufferAccess {
 public:
  struct Dependency {
    VkPipelineStageFlags stages;
    VkAccessFlags access;
  };
  std::optional<Dependency> Use(VkPipelineStageFlags stages, VkAccessFlags access, bool force = false) {
    constexpr VkAccessFlags kWrites = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
        VK_ACCESS_HOST_WRITE_BIT;
    const auto writes = access & kWrites;
    std::optional<Dependency> dependency;
    if (force || writes) {
      dependency = Dependency{stages_, access_};
    } else if (writer_access_) {
      for (auto remaining = stages; remaining; remaining &= remaining - 1) {
        if ((visible_access_[std::countr_zero(remaining)] & access) != access) {
          dependency = Dependency{writer_stages_, writer_access_};
          break;
        }
      }
    }
    if (writes) {
      stages_ = writer_stages_ = stages;
      access_ = access;
      writer_access_ = writes;
      visible_access_.fill(0);
    } else {
      stages_ |= stages;
      access_ |= access;
      if (dependency) {
        for (auto remaining = stages; remaining; remaining &= remaining - 1)
          visible_access_[std::countr_zero(remaining)] |= access;
      }
    }
    return dependency;
  }
 private:
  VkPipelineStageFlags stages_ = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkAccessFlags access_ = 0;
  VkPipelineStageFlags writer_stages_ = 0;
  VkAccessFlags writer_access_ = 0;
  // Preserve stage/access pairs, not just their independent unions. For
  // example VS uniform reads and FS storage reads do not cover VS storage.
  std::array<VkAccessFlags, 32> visible_access_{};
};
}
