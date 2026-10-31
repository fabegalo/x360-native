#pragma once
#include "../pipeline/deferred_commands.h"
#include <rex/ui/vulkan/device.h>
#include <array>
#include <stdexcept>
#include <vector>
namespace x360n::native {
// The vkCmd* subset Native records, through DeferredCommands (same contract
// as D3D12Commands): direct while no bound pipeline is pending, otherwise kept
// in order with copies of every pointed-to argument and replayed into the same
// command buffer before it is ended. Vulkan objects used by recorded commands
// already outlive the submission's fence, which is after replay.
class VulkanCommands {
 public:
  using Functions = rex::ui::vulkan::VulkanDevice::Functions;
  explicit VulkanCommands(const Functions& fn) : fn_(fn) {}
  void Attach(VkCommandBuffer buffer) { commands_.Attach(buffer); }
  bool deferred() const { return commands_.deferred(); }
  void Replay() { commands_.Replay(); }

  void BindPipeline(VkPipelineBindPoint point, VkPipeline pipeline) {
    Rec([=, f = fn_.vkCmdBindPipeline](VkCommandBuffer b) { f(b, point, pipeline); });
  }
  void BindPipeline(VkPipelineBindPoint point, const std::shared_future<VkPipeline>& pipeline) {
    commands_.BindPending(pipeline, [point, f = fn_.vkCmdBindPipeline](VkCommandBuffer b, VkPipeline p) { f(b, point, p); });
  }
  void BindDescriptorSets(VkPipelineBindPoint point, VkPipelineLayout layout, uint32_t first, uint32_t count,
                          const VkDescriptorSet* sets, uint32_t dynamic_count, const uint32_t* dynamic) {
    if (Direct()) return fn_.vkCmdBindDescriptorSets(Buffer(), point, layout, first, count, sets, dynamic_count, dynamic);
    std::vector<VkDescriptorSet> s(sets, sets + count);
    std::vector<uint32_t> d(dynamic, dynamic + dynamic_count);
    Rec([=, f = fn_.vkCmdBindDescriptorSets, s = std::move(s), d = std::move(d)](VkCommandBuffer b) {
      f(b, point, layout, first, uint32_t(s.size()), s.data(), uint32_t(d.size()), d.empty() ? nullptr : d.data());
    });
  }
  void PushConstants(VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void* data) {
    if (Direct()) return fn_.vkCmdPushConstants(Buffer(), layout, stages, offset, size, data);
    std::vector<uint8_t> bytes(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    Rec([=, f = fn_.vkCmdPushConstants, bytes = std::move(bytes)](VkCommandBuffer b) {
      f(b, layout, stages, offset, uint32_t(bytes.size()), bytes.data());
    });
  }
  void Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    Rec([=, f = fn_.vkCmdDispatch](VkCommandBuffer b) { f(b, x, y, z); });
  }
  void SetViewport(uint32_t first, uint32_t count, const VkViewport* viewports) {
    if (Direct()) return fn_.vkCmdSetViewport(Buffer(), first, count, viewports);
    std::vector<VkViewport> v(viewports, viewports + count);
    Rec([=, f = fn_.vkCmdSetViewport, v = std::move(v)](VkCommandBuffer b) { f(b, first, uint32_t(v.size()), v.data()); });
  }
  void SetScissor(uint32_t first, uint32_t count, const VkRect2D* rects) {
    if (Direct()) return fn_.vkCmdSetScissor(Buffer(), first, count, rects);
    std::vector<VkRect2D> r(rects, rects + count);
    Rec([=, f = fn_.vkCmdSetScissor, r = std::move(r)](VkCommandBuffer b) { f(b, first, uint32_t(r.size()), r.data()); });
  }
  void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type) {
    Rec([=, f = fn_.vkCmdBindIndexBuffer](VkCommandBuffer b) { f(b, buffer, offset, type); });
  }
  void Draw(uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance) {
    Rec([=, f = fn_.vkCmdDraw](VkCommandBuffer b) { f(b, vertices, instances, first_vertex, first_instance); });
  }
  void DrawIndexed(uint32_t indices, uint32_t instances, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
    Rec([=, f = fn_.vkCmdDrawIndexed](VkCommandBuffer b) { f(b, indices, instances, first_index, vertex_offset, first_instance); });
  }
  void CopyBuffer(VkBuffer src, VkBuffer dst, uint32_t count, const VkBufferCopy* regions) {
    if (Direct()) return fn_.vkCmdCopyBuffer(Buffer(), src, dst, count, regions);
    std::vector<VkBufferCopy> r(regions, regions + count);
    Rec([=, f = fn_.vkCmdCopyBuffer, r = std::move(r)](VkCommandBuffer b) { f(b, src, dst, uint32_t(r.size()), r.data()); });
  }
  void CopyBufferToImage(VkBuffer src, VkImage dst, VkImageLayout layout, uint32_t count, const VkBufferImageCopy* regions) {
    if (Direct()) return fn_.vkCmdCopyBufferToImage(Buffer(), src, dst, layout, count, regions);
    std::vector<VkBufferImageCopy> r(regions, regions + count);
    Rec([=, f = fn_.vkCmdCopyBufferToImage, r = std::move(r)](VkCommandBuffer b) {
      f(b, src, dst, layout, uint32_t(r.size()), r.data());
    });
  }
  void ClearColorImage(VkImage image, VkImageLayout layout, const VkClearColorValue* color, uint32_t count,
                       const VkImageSubresourceRange* ranges) {
    if (Direct()) return fn_.vkCmdClearColorImage(Buffer(), image, layout, color, count, ranges);
    const VkClearColorValue c = *color;
    std::vector<VkImageSubresourceRange> r(ranges, ranges + count);
    Rec([=, f = fn_.vkCmdClearColorImage, r = std::move(r)](VkCommandBuffer b) {
      f(b, image, layout, &c, uint32_t(r.size()), r.data());
    });
  }
  void FillBuffer(PFN_vkCmdFillBuffer fill, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data) {
    Rec([=](VkCommandBuffer b) { fill(b, buffer, offset, size, data); });
  }
  void PipelineBarrier(VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags flags,
                       uint32_t memory_count, const VkMemoryBarrier* memory,
                       uint32_t buffer_count, const VkBufferMemoryBarrier* buffers,
                       uint32_t image_count, const VkImageMemoryBarrier* images) {
    if (Direct())
      return fn_.vkCmdPipelineBarrier(Buffer(), src, dst, flags, memory_count, memory, buffer_count, buffers, image_count, images);
    std::vector<VkMemoryBarrier> m(memory, memory + memory_count);
    std::vector<VkBufferMemoryBarrier> bb(buffers, buffers + buffer_count);
    std::vector<VkImageMemoryBarrier> ib(images, images + image_count);
    for (const auto& barrier : m) RequireNoChain(barrier.pNext);
    for (const auto& barrier : bb) RequireNoChain(barrier.pNext);
    for (const auto& barrier : ib) RequireNoChain(barrier.pNext);
    Rec([=, f = fn_.vkCmdPipelineBarrier, m = std::move(m), bb = std::move(bb), ib = std::move(ib)](VkCommandBuffer b) {
      f(b, src, dst, flags, uint32_t(m.size()), m.empty() ? nullptr : m.data(), uint32_t(bb.size()),
        bb.empty() ? nullptr : bb.data(), uint32_t(ib.size()), ib.empty() ? nullptr : ib.data());
    });
  }
  void BeginRenderPass(const VkRenderPassBeginInfo* info, VkSubpassContents contents) {
    if (Direct()) return fn_.vkCmdBeginRenderPass(Buffer(), info, contents);
    RequireNoChain(info->pNext);
    std::vector<VkClearValue> clears(info->pClearValues, info->pClearValues + info->clearValueCount);
    const VkRenderPassBeginInfo copy = *info;
    Rec([=, f = fn_.vkCmdBeginRenderPass, clears = std::move(clears)](VkCommandBuffer b) {
      auto begin = copy;
      begin.pClearValues = clears.empty() ? nullptr : clears.data();
      f(b, &begin, contents);
    });
  }
  void EndRenderPass() { Rec([f = fn_.vkCmdEndRenderPass](VkCommandBuffer b) { f(b); }); }
 private:
  template <class F> void Rec(F&& f) { commands_.Record(std::forward<F>(f)); }
  bool Direct() const { return !commands_.deferred(); }
  VkCommandBuffer Buffer() const { return commands_.list(); }
  // Extension structures are not deep-copied; Native passes none here.
  static void RequireNoChain(const void* next) {
    if (next) throw std::logic_error("Native Vulkan: deferred command with an extension chain");
  }
  const Functions& fn_;
  DeferredCommands<VkCommandBuffer_T> commands_;
};
}
