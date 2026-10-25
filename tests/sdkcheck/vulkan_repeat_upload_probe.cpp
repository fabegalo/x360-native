// 0196: execute production barrier batching and deferred Vulkan command replay.
// The generated include is verbatim SDK code, not a second barrier algorithm.
// Allocation/texture construction is replaced; no device, driver or game runs.
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/util.h>
#include <rex/graphics/vulkan/deferred_command_buffer.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <vector>

namespace rex::graphics::vulkan {
struct CaptureDevice {
  ui::vulkan::VulkanDevice::Functions functions_{};
  const auto& functions() const { return functions_; }
};

// Minimal owned state for the production methods extracted at build time.
// No production constructor is invoked, so no GPU resources are allocated.
class VulkanCommandProcessor {
 public:
  CaptureDevice device;
  CaptureDevice* GetVulkanDevice() const { return const_cast<CaptureDevice*>(&device); }
  bool PushImageMemoryBarrier(VkImage, const VkImageSubresourceRange&,
      VkPipelineStageFlags, VkPipelineStageFlags, VkAccessFlags, VkAccessFlags,
      VkImageLayout, VkImageLayout, uint32_t = VK_QUEUE_FAMILY_IGNORED,
      uint32_t = VK_QUEUE_FAMILY_IGNORED, bool = true,
      const VkSampleLocationsInfoEXT* = nullptr);
  bool SubmitBarriers(bool);
  void SplitPendingBarrier();
  void EndRenderPass();
  void EndActiveForPass(bool use_dynamic_rendering);
  bool submission_open_ = true, in_render_pass_ = false, hoist_render_pass_ = false;
  VkRenderPass current_render_pass_ = VK_NULL_HANDLE;
  void* current_framebuffer_ = nullptr;
  std::vector<VkBufferMemoryBarrier> pending_barriers_buffer_memory_barriers_;
  std::vector<VkImageMemoryBarrier> pending_barriers_image_memory_barriers_;
  struct PendingBarrier {
    VkPipelineStageFlags src_stage_mask = 0, dst_stage_mask = 0;
    size_t buffer_memory_barriers_offset = 0, image_memory_barriers_offset = 0;
  };
  std::vector<PendingBarrier> pending_barriers_;
  PendingBarrier current_pending_barrier_;
  DeferredCommandBuffer deferred_command_buffer_{*this};
};

class VulkanTextureCache {
 public:
  struct VulkanTexture {
    enum class Usage { kUndefined, kTransferDestination, kGuestShaderSampled, kSwapSampled };
    Usage usage = Usage::kUndefined;
    Usage SetUsage(Usage next) { auto previous = usage; usage = next; return previous; }
    void MarkAsUsed() {}  // Resource lifetime accounting is outside this CPU probe.
    VkImage image() const { return reinterpret_cast<VkImage>(uintptr_t(1)); }
  };
  VulkanCommandProcessor& command_processor_;
  VkPipelineStageFlags guest_shader_pipeline_stages_ = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  void GetTextureUsageMasks(VulkanTexture::Usage, VkPipelineStageFlags&, VkAccessFlags&,
                            VkImageLayout&);
  void RecordUpload(VulkanTexture&);
};
}  // namespace rex::graphics::vulkan

#include "vulkan_repeat_upload_backend.inc"

using namespace rex::graphics::vulkan;
namespace {
struct Event {
  bool copy;
  VkPipelineStageFlags src = 0, dst = 0;
  VkImageMemoryBarrier barrier{};
  VkImage image = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
std::vector<Event> events;
bool captured_depth_locations = false;
float captured_location_x = 0;
bool captured_pass_locations = false;
unsigned ended_render_passes = 0, ended_dynamic_rendering = 0;
VKAPI_ATTR void VKAPI_CALL EndPass(VkCommandBuffer) { ++ended_render_passes; }
VKAPI_ATTR void VKAPI_CALL EndDynamic(VkCommandBuffer) { ++ended_dynamic_rendering; }
VKAPI_ATTR void VKAPI_CALL Barrier(VkCommandBuffer, VkPipelineStageFlags src,
    VkPipelineStageFlags dst, VkDependencyFlags, uint32_t, const VkMemoryBarrier*,
    uint32_t, const VkBufferMemoryBarrier*, uint32_t count, const VkImageMemoryBarrier* images) {
  for (uint32_t i = 0; i < count; ++i) {
    events.push_back({false, src, dst, images[i]});
    if (images[i].pNext) {
      const auto& info = *static_cast<const VkSampleLocationsInfoEXT*>(images[i].pNext);
      captured_depth_locations = info.sType == VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT &&
          info.sampleLocationsPerPixel == VK_SAMPLE_COUNT_2_BIT && info.sampleLocationsCount == 2;
      captured_location_x = info.pSampleLocations[0].x;
    }
  }
}
VKAPI_ATTR void VKAPI_CALL BeginPass(VkCommandBuffer, const VkRenderPassBeginInfo* begin,
                                     VkSubpassContents) {
  const auto* info = static_cast<const VkRenderPassSampleLocationsBeginInfoEXT*>(begin->pNext);
  captured_pass_locations = info && info->attachmentInitialSampleLocationsCount == 1 &&
      info->postSubpassSampleLocationsCount == 1 &&
      info->pAttachmentInitialSampleLocations[0].attachmentIndex == 0 &&
      info->pPostSubpassSampleLocations[0].sampleLocationsInfo.pSampleLocations[0].x == 0.25f;
}
VKAPI_ATTR void VKAPI_CALL Copy(VkCommandBuffer, VkBuffer, VkImage image, VkImageLayout layout,
                                uint32_t, const VkBufferImageCopy*) {
  events.push_back({true, 0, 0, {}, image, layout});
}
}  // namespace

int main() {
  int checks = 0, failures = 0;
  auto check = [&](bool ok, const char* message) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
  };
  VulkanCommandProcessor cp;
  cp.device.functions_.vkCmdPipelineBarrier = Barrier;
  cp.device.functions_.vkCmdCopyBufferToImage = Copy;
  cp.device.functions_.vkCmdBeginRenderPass = BeginPass;
  cp.device.functions_.vkCmdEndRenderPass = EndPass;
  cp.device.functions_.vkCmdEndRendering = EndDynamic;
  VulkanTextureCache cache{cp};
  VulkanTextureCache::VulkanTexture texture;
  using Usage = VulkanTextureCache::VulkanTexture::Usage;
  auto replay = [&] {
    events.clear();
    cp.deferred_command_buffer_.Execute(VK_NULL_HANDLE);
    cp.deferred_command_buffer_.Reset();
  };

  // Two uploads, same image, one open submission. Replay has no queue submit.
  cache.RecordUpload(texture);
  cache.RecordUpload(texture);
  replay();
  check(events.size() == 4, "transition, copy, write/write dependency, copy");
  if (events.size() == 4) {
    check(!events[0].copy && events[1].copy && !events[2].copy && events[3].copy,
          "repeat barrier executes between the copies");
    check(events[0].barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
          events[0].barrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          "initial layout transition preserved");
    const auto& e = events[2];
    check(e.src == VK_PIPELINE_STAGE_TRANSFER_BIT && e.dst == VK_PIPELINE_STAGE_TRANSFER_BIT,
          "repeat dependency has TRANSFER stages");
    check(e.barrier.srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT &&
          e.barrier.dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT, "write/write access masks");
    check(e.barrier.oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
          e.barrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, "repeat layout preserved");
    check(e.barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
          e.barrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED, "no queue ownership transfer");
    check(e.barrier.image == events[1].image && e.barrier.image == events[3].image,
          "dependency targets the twice-uploaded image");
    check(e.barrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
          e.barrier.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS &&
          e.barrier.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS,
          "all mip levels and array layers protected");
  }
  check(cp.pending_barriers_.empty() && cp.pending_barriers_image_memory_barriers_.empty(),
        "barrier batching drained before copies");

  texture.usage = Usage::kGuestShaderSampled;
  cache.RecordUpload(texture);
  replay();
  check(events.size() == 2 && !events[0].copy && events[1].copy,
        "real transition still emits exactly one barrier before copy");
  if (events.size() == 2) {
    const auto& e = events[0];
    check(e.barrier.oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
          e.barrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
          e.barrier.srcAccessMask == VK_ACCESS_SHADER_READ_BIT &&
          e.barrier.dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT, "sampled-to-transfer masks/layout");
  }

  // The general no-op elision remains; only the repeated write opts out.
  check(!cp.PushImageMemoryBarrier(texture.image(), rex::ui::vulkan::util::InitializeSubresourceRange(),
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
        "default equality elision unchanged");
  check(!cp.SubmitBarriers(true), "no pending barrier after elision");

  // 0156: unrelated hoisted commands keep their position before the main stream.
  auto point = cp.deferred_command_buffer_.CmdHoistPoint();
  cache.RecordUpload(texture);
  cp.deferred_command_buffer_.SetWritingHoisted(true);
  VkBufferImageCopy hoisted_copy{};
  hoisted_copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  hoisted_copy.imageExtent = {1, 1, 1};
  cp.deferred_command_buffer_.CmdVkCopyBufferToImage(reinterpret_cast<VkBuffer>(uintptr_t(2)),
      reinterpret_cast<VkImage>(uintptr_t(3)), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &hoisted_copy);
  cp.deferred_command_buffer_.SetWritingHoisted(false);
  cp.deferred_command_buffer_.ExtendHoistPoint(point);
  cache.RecordUpload(texture);
  replay();
  check(events.size() == 5 && events[0].copy && events[0].image == reinterpret_cast<VkImage>(uintptr_t(3)) &&
        !events[1].copy && events[2].copy && !events[3].copy && events[4].copy,
        "hoisted stream does not move repeat-image barrier past its copy");

  // 0197: deferred commands own their sample arrays, including worker replay.
  VkSampleLocationEXT locations[2] = {{0.25f, 0.75f}, {0.75f, 0.25f}};
  VkSampleLocationsInfoEXT info{VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT};
  info.sampleLocationsPerPixel = VK_SAMPLE_COUNT_2_BIT;
  info.sampleLocationGridSize = {1, 1}; info.sampleLocationsCount = 2;
  info.pSampleLocations = locations;
  cp.PushImageMemoryBarrier(texture.image(), rex::ui::vulkan::util::InitializeSubresourceRange(
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, true, &info);
  cp.SubmitBarriers(true);
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  cp.deferred_command_buffer_.CmdVkBeginRenderPass(&begin, VK_SUBPASS_CONTENTS_INLINE, &info, true);
  DeferredCommandBuffer::Streams streams;
  cp.deferred_command_buffer_.TakeStreams(streams);
  locations[0].x = 0.875f;  // Must not affect already recorded commands.
  cp.deferred_command_buffer_.ExecuteStreams(VK_NULL_HANDLE, streams);
  check(captured_depth_locations && captured_location_x == 0.25f,
        "depth barrier owns sample locations through TakeStreams worker replay");
  check(captured_pass_locations, "render pass owns initial/post-subpass depth sample locations");
  cp.in_render_pass_ = true;
  cp.current_render_pass_ = reinterpret_cast<VkRenderPass>(uintptr_t(1));
  cp.EndActiveForPass(true);  // New draw uses dynamic rendering, old one did not.
  replay();
  cp.in_render_pass_ = true;
  cp.current_render_pass_ = VK_NULL_HANDLE;
  cp.EndActiveForPass(false);  // New draw needs a sample-locations render pass.
  replay();
  check(ended_render_passes == 1 && ended_dynamic_rendering == 1,
        "MSAA sample-pattern changes close the previous pass using its own API");
  std::cout << "Vulkan repeat upload CPU capture: " << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
