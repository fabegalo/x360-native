#include "vulkan_render.h"
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/vulkan/instance.h>
#include <Windows.h>
#include <rex/logging.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace x360n::native {
VulkanRender::VulkanRender(rex::ui::vulkan::VulkanProvider& provider)
    : device_(*provider.vulkan_device()), submissions_(device_) {
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  try {
    if (edram_size_+sizeof(SampleCounts)>device_.properties().maxStorageBufferRange)
      throw std::runtime_error("Native: requested scale exceeds device EDRAM storage buffer range");
    guest_buffer_ = std::make_unique<VulkanBuffer>(device_, GuestMemoryMirror::kSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VulkanBuffer::Access::kDevice);
    edram_buffer_ = std::make_unique<VulkanBuffer>(device_, edram_size_ + sizeof(SampleCounts),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanBuffer::Access::kDevice);
    readback_ = std::make_unique<VulkanBuffer>(device_, 16ull << 20, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             VulkanBuffer::Access::kReadback);
    VkPipelineCacheCreateInfo cache{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    CheckVulkan(LoadNativeVulkanFunction<PFN_vkCreatePipelineCache>(device_, "vkCreatePipelineCache")(
        vk, &cache, nullptr, &pipeline_cache_), "create Native pipeline cache");
    for (auto& arena : arenas_) {
      const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16384},
          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16384}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 32768},
          {VK_DESCRIPTOR_TYPE_SAMPLER, 32768}, {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 4096}};
      VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      pool.maxSets = 4096;
      pool.poolSizeCount = uint32_t(std::size(sizes));
      pool.pPoolSizes = sizes;
      CheckVulkan(fn.vkCreateDescriptorPool(vk, &pool, nullptr, &arena.descriptors), "create render descriptor arena");
    }
    const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                               VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount = 1;
    set.pBindings = &binding;
    CheckVulkan(fn.vkCreateDescriptorSetLayout(vk, &set, nullptr, &storage_layout_), "create resolve storage layout");
    const VkDescriptorSetLayout copy_sets[]{storage_layout_, storage_layout_};
    VkPushConstantRange constants{VK_SHADER_STAGE_COMPUTE_BIT, 0,
        sizeof(rex::graphics::draw_util::ResolveCopyShaderConstants)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 2;
    layout.pSetLayouts = copy_sets;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &constants;
    CheckVulkan(fn.vkCreatePipelineLayout(vk, &layout, nullptr, &copy_layout_), "create resolve copy layout");
    constants.size = 6 * sizeof(uint32_t);
    CheckVulkan(fn.vkCreatePipelineLayout(vk, &layout, nullptr, &downscale_layout_), "create scaled resolve publication layout");
    layout.setLayoutCount = 1;
    constants.size = sizeof(rex::graphics::draw_util::ResolveClearShaderConstants);
    CheckVulkan(fn.vkCreatePipelineLayout(vk, &layout, nullptr, &clear_layout_), "create resolve clear layout");
    Begin();
    UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    LoadNativeVulkanFunction<PFN_vkCmdFillBuffer>(device_, "vkCmdFillBuffer")(
        commands_, edram_buffer_->handle(), 0, VK_WHOLE_SIZE, 0);
  } catch (...) { Destroy(); throw; }
}
VulkanRender::~VulkanRender() {
  try { Flush(true); } catch (const std::exception& error) {
    REX_FATAL("Native Vulkan: unsafe render teardown: {}", error.what());
  }
  Destroy();
}
void VulkanRender::Destroy() {
  DestroyDraw();
  DestroyTextures();
  scaled_.Clear();
  mirror_.reset();
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  SavePipelineCache();
  if (pipeline_cache_) LoadNativeVulkanFunction<PFN_vkDestroyPipelineCache>(device_, "vkDestroyPipelineCache")(
      vk, pipeline_cache_, nullptr);
  pipeline_cache_ = VK_NULL_HANDLE;
  for (auto pipeline : copy_pipelines_) if (pipeline) fn.vkDestroyPipeline(vk, pipeline, nullptr);
  for (auto pipeline : clear_pipelines_) if (pipeline) fn.vkDestroyPipeline(vk, pipeline, nullptr);
  for (auto& pipeline : load_pipelines_) { if (pipeline) fn.vkDestroyPipeline(vk, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }
  if (load_layout_) fn.vkDestroyPipelineLayout(vk, load_layout_, nullptr);
  load_layout_ = VK_NULL_HANDLE;
  if (copy_layout_) fn.vkDestroyPipelineLayout(vk, copy_layout_, nullptr);
  if (clear_layout_) fn.vkDestroyPipelineLayout(vk, clear_layout_, nullptr);
  if (downscale_pipeline_) fn.vkDestroyPipeline(vk, downscale_pipeline_, nullptr);
  if (downscale_layout_) fn.vkDestroyPipelineLayout(vk, downscale_layout_, nullptr);
  downscale_pipeline_=VK_NULL_HANDLE; downscale_layout_=VK_NULL_HANDLE;
  if (storage_layout_) fn.vkDestroyDescriptorSetLayout(vk, storage_layout_, nullptr);
  for (auto& arena : arenas_) {
    if (arena.descriptors) fn.vkDestroyDescriptorPool(vk, arena.descriptors, nullptr);
    arena.descriptors = VK_NULL_HANDLE;
    arena.uploads.clear();
    arena.load_scratch.clear();
  }
  copy_pipelines_.fill(VK_NULL_HANDLE);
  clear_pipelines_.fill(VK_NULL_HANDLE);
  copy_layout_ = clear_layout_ = VK_NULL_HANDLE;
  storage_layout_ = VK_NULL_HANDLE;
  readback_.reset();
  edram_buffer_.reset();
  guest_buffer_.reset();
}
void VulkanRender::Begin() {
  if (commands_) return;
  commands_ = submissions_.Begin();
  auto& arena = arenas_[submissions_.current_slot()];
  CheckVulkan(device_.functions().vkResetDescriptorPool(device_.device(), arena.descriptors, 0),
              "reset render descriptor arena");
  arena.descriptor_count = 0;
  arena.image_descriptors = arena.sampler_descriptors = 0;
  arena.upload_index = 0;
  for (auto& page : arena.uploads) page.used = 0;
  // The pool reset above released the load descriptor sets of this slot.
  for (auto& page : arena.load_scratch) { page.used = 0; page.load_set = VK_NULL_HANDLE; }
  arena.guest_load_set = VK_NULL_HANDLE;
}
void VulkanRender::EndRendering() {
  if (!rendering_) return;
  device_.functions().vkCmdEndRenderPass(commands_);
  rendering_ = false;
}
void VulkanRender::Flush(bool wait) {
  if (commands_) {
    EndRendering();
    submissions_.Submit();
    commands_ = VK_NULL_HANDLE;
  }
  if (wait || !writeback_.empty() || !scaled_readbacks_.empty()) submissions_.Drain();
  if (!writeback_.empty()) {
    readback_->Invalidate(0, readback_used_);
    writeback_.Complete([&](const GuestWriteback::Write& write) {
      mirror_->CopyReadbackToGuest(write.address,
          reinterpret_cast<const uint8_t*>(readback_->mapped() + write.offset), write.length, write.cpu_generations);
    });
    readback_used_ = 0;
  }
  for (const auto& scaled : scaled_readbacks_) {
    const size_t bytes=size_t(scaled.ticket.length)*scale_x_*scale_y_;
    scaled.buffer->Invalidate(scaled.offset, bytes);
    scaled_.Complete(scaled.ticket, {reinterpret_cast<const uint8_t*>(scaled.buffer->mapped()+scaled.offset), bytes},
                     scaled.pixel_log2, *memory_);
  }
  scaled_readbacks_.clear();
}
VulkanRender::UploadSlice VulkanRender::Upload(const void* data, VkDeviceSize size, VkDeviceSize alignment) {
  if (!data || !size || !alignment) throw std::invalid_argument("Native Vulkan: invalid upload");
  Begin();
  auto& arena = arenas_[submissions_.current_slot()];
  for (;;) {
    if (arena.upload_index == arena.uploads.size()) {
      constexpr VkDeviceSize kPage = 4ull << 20;
      arena.uploads.push_back({std::make_unique<VulkanBuffer>(device_, std::max(kPage, size + alignment),
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
          VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VulkanBuffer::Access::kUpload), 0});
    }
    auto& page = arena.uploads[arena.upload_index];
    const auto offset = ((page.used + alignment - 1) / alignment) * alignment;
    if (offset <= page.buffer->size() && size <= page.buffer->size() - offset) {
      std::memcpy(page.buffer->mapped() + offset, data, size_t(size));
      page.buffer->Flush(offset, size);
      page.used = offset + size;
      return {page.buffer->handle(), offset, page.buffer->mapped() + offset};
    }
    ++arena.upload_index;
  }
}
VkDescriptorSet VulkanRender::AllocateDescriptors(VkDescriptorSetLayout layout) {
  Begin();
  auto& arena = arenas_[submissions_.current_slot()];
  // Callers reserve enough descriptors before recording an indivisible draw
  // or resolve. Flushing inside this function could invalidate its bindings.
  if (arena.descriptor_count >= 4096) throw std::runtime_error("Native Vulkan: descriptor arena overflow");
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = arena.descriptors;
  allocate.descriptorSetCount = 1;
  allocate.pSetLayouts = &layout;
  VkDescriptorSet result = VK_NULL_HANDLE;
  CheckVulkan(device_.functions().vkAllocateDescriptorSets(device_.device(), &allocate, &result),
              "allocate render descriptors");
  ++arena.descriptor_count;
  return result;
}
VkPipeline VulkanRender::ComputePipeline(VkPipelineLayout layout, std::span<const uint32_t> spirv) {
  const auto module = CreateNativeShaderModule(device_, spirv);
  VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  info.stage.module = module;
  info.stage.pName = "main";
  info.layout = layout;
  VkPipeline result = VK_NULL_HANDLE;
  const auto status = device_.functions().vkCreateComputePipelines(device_.device(), pipeline_cache_,
                                                                  1, &info, nullptr, &result);
  device_.functions().vkDestroyShaderModule(device_.device(), module, nullptr);
  CheckVulkan(status, "create resolve compute pipeline");
  return result;
}
void VulkanRender::SetPipelineDirectory(const std::filesystem::path& directory) {
  const auto file = directory.empty() ? std::filesystem::path{} : directory / "pipelines.bin";
  if (file == pipeline_file_) return;
  SavePipelineCache();
  pipeline_file_ = file;
  if (file.empty()) return;
  std::ifstream stream(file, std::ios::binary | std::ios::ate);
  if (!stream) return;
  const auto length = stream.tellg();
  if (length < 32 || length > (64 << 20)) return;
  std::vector<uint8_t> bytes(static_cast<size_t>(length));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char*>(bytes.data()), length)) return;
  uint32_t header[4];
  std::memcpy(header, bytes.data(), sizeof(header));
  VkPhysicalDeviceProperties props;
  device_.vulkan_instance()->functions().vkGetPhysicalDeviceProperties(device_.physical_device(), &props);
  if (header[0] != 32 || header[1] != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
      header[2] != props.vendorID || header[3] != props.deviceID ||
      std::memcmp(bytes.data() + 16, props.pipelineCacheUUID, VK_UUID_SIZE)) return;
  VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
  info.initialDataSize = bytes.size();
  info.pInitialData = bytes.data();
  VkPipelineCache loaded = VK_NULL_HANDLE;
  if (LoadNativeVulkanFunction<PFN_vkCreatePipelineCache>(device_, "vkCreatePipelineCache")(
      device_.device(), &info, nullptr, &loaded) != VK_SUCCESS) return;
  const auto status = LoadNativeVulkanFunction<PFN_vkMergePipelineCaches>(device_, "vkMergePipelineCaches")(
      device_.device(), pipeline_cache_, 1, &loaded);
  LoadNativeVulkanFunction<PFN_vkDestroyPipelineCache>(device_, "vkDestroyPipelineCache")(
      device_.device(), loaded, nullptr);
  if (status != VK_SUCCESS) REXLOG_WARN("Native Vulkan: pipeline cache merge failed ({})", int(status));
}
void VulkanRender::SavePipelineCache() {
  if (pipeline_file_.empty() || !pipeline_cache_) return;
  const auto get_data = LoadNativeVulkanFunction<PFN_vkGetPipelineCacheData>(device_, "vkGetPipelineCacheData");
  size_t size = 0;
  if (get_data(device_.device(), pipeline_cache_, &size, nullptr) != VK_SUCCESS ||
      !size || size > (64 << 20)) return;
  std::vector<uint8_t> bytes(size);
  if (get_data(device_.device(), pipeline_cache_, &size, bytes.data()) != VK_SUCCESS) return;
  std::error_code error;
  std::filesystem::create_directories(pipeline_file_.parent_path(), error);
  if (error) return;
  const auto temporary = std::filesystem::path(pipeline_file_.wstring() + L".tmp");
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream.write(reinterpret_cast<const char*>(bytes.data()), size)) return;
  stream.close();
  // Cache contents are optional and device-specific; a failed replacement must
  // never interfere with guest execution or discard the preceding valid file.
  if (!MoveFileExW(temporary.c_str(), pipeline_file_.c_str(), MOVEFILE_REPLACE_EXISTING))
    REXLOG_WARN("Native Vulkan: unable to persist pipeline cache");
}
SampleCounts VulkanRender::ReadSampleCounts(bool counting_after) {
  Begin();
  EndRendering();
  // Resolve readbacks occupy the same staging buffer. Publish them before
  // reusing its first 32 bytes for this independently observable snapshot.
  if (!writeback_.empty()) Flush(true);
  Begin();
  UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  const VkBufferCopy copy{edram_size_, 0, sizeof(SampleCounts)};
  const auto& fn = device_.functions();
  fn.vkCmdCopyBuffer(commands_, edram_buffer_->handle(), readback_->handle(), 1, &copy);
  VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  host.buffer = readback_->handle();
  host.size = sizeof(SampleCounts);
  fn.vkCmdPipelineBarrier(commands_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
      0, 0, nullptr, 1, &host, 0, nullptr);
  Flush(true);
  readback_->Invalidate(0, sizeof(SampleCounts));
  SampleCounts result;
  std::memcpy(result.data(), readback_->mapped(), sizeof(result));
  counting_samples_ = counting_after;
  return result;
}
void VulkanRender::SetSampleCounts(const SampleCounts& counts, bool visibility_only) {
  Begin();
  const auto upload = Upload(counts.data(), sizeof(counts), 4);
  EndRendering();
  UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
  const VkBufferCopy copy{upload.offset, edram_size_, sizeof(counts)};
  device_.functions().vkCmdCopyBuffer(commands_, upload.buffer, edram_buffer_->handle(), 1, &copy);
  visibility_only_ = visibility_only;
}
GuestMemoryMirror& VulkanRender::Mirror(rex::memory::Memory& memory) {
  if (memory_ && memory_ != &memory) throw std::logic_error("Native Vulkan: guest memory domain changed");
  memory_ = &memory;
  if (!mirror_) mirror_ = std::make_unique<GuestMemoryMirror>(memory,
      [this](uint32_t start, uint32_t length) { UploadGuest(start, length); });
  scaled_.Attach(*mirror_);
  return *mirror_;
}
void VulkanRender::UploadGuest(uint32_t address, uint32_t length) {
  const auto upload = Upload(memory_->TranslatePhysical(address), length, 4);
  EndRendering();
  UseBuffer(guest_buffer_->handle(), guest_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
  const VkBufferCopy copy{upload.offset, address, length};
  device_.functions().vkCmdCopyBuffer(commands_, upload.buffer, guest_buffer_->handle(), 1, &copy);
}
void VulkanRender::UseBuffer(VkBuffer buffer, BufferAccess& previous,
                             VkPipelineStageFlags stage, VkAccessFlags access, bool force) {
  Begin();
  constexpr VkAccessFlags kWrites = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
  if (!force && !(previous.access & kWrites) && !(access & kWrites)) {
    previous.stage |= stage;
    previous.access |= access;
    return;
  }
  EndRendering();
  if (buffer == edram_buffer_->handle()) edram_draw_layout_.reset();
  VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  barrier.srcAccessMask = previous.access;
  barrier.dstAccessMask = access;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = buffer;
  barrier.size = VK_WHOLE_SIZE;
  device_.functions().vkCmdPipelineBarrier(commands_, previous.stage, stage, 0,
                                           0, nullptr, 1, &barrier, 0, nullptr);
  previous = {stage, access};
}
}
