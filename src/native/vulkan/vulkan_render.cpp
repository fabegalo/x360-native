#include "vulkan_render.h"
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/vulkan/instance.h>
#if defined(_WIN32)
#include <Windows.h>
#endif
#include <rex/logging.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace x360n::native {
VulkanRender::VulkanRender(rex::ui::vulkan::VulkanProvider& provider)
    : device_(*provider.vulkan_device()), submissions_(device_),
      pipeline_workers_(std::make_unique<PipelineWorkers>(NativePipelineThreads())) {
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  try {
    if (edram_size_+sizeof(SampleCounts)>device_.properties().maxStorageBufferRange)
      throw std::runtime_error("Native: requested scale exceeds device EDRAM storage buffer range");
    guest_buffer_ = std::make_unique<VulkanBuffer>(device_, GuestMemoryMirror::kSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VulkanBuffer::Access::kDevice);
    edram_buffer_ = std::make_unique<VulkanBuffer>(device_, edram_size_ + sizeof(SampleCounts),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanBuffer::Access::kDevice);
    VkPipelineCacheCreateInfo cache{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    CheckVulkan(LoadNativeVulkanFunction<PFN_vkCreatePipelineCache>(device_, "vkCreatePipelineCache")(
        vk, &cache, nullptr, &pipeline_cache_), "create Native pipeline cache");
    for (auto& arena : arenas_) arena.descriptors = CreateDescriptorPool();
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
    cmd_.FillBuffer(LoadNativeVulkanFunction<PFN_vkCmdFillBuffer>(device_, "vkCmdFillBuffer"),
        edram_buffer_->handle(), 0, VK_WHOLE_SIZE, 0);
  } catch (...) { Destroy(); throw; }
}
VulkanRender::~VulkanRender() {
  try { Flush(true); } catch (const std::exception& error) {
    REX_FATAL("Native Vulkan: unsafe render teardown: {}", error.what());
  }
  Destroy();
}
void VulkanRender::EndFrame() {
  if (mirror_) mirror_->InvalidateCpuPages();
  auto& perf = NativeVulkanPerformance();
  if (perf.enabled && ++perf.presents % 120 == 0)
    REXLOG_INFO("Native Vulkan draw indices presents={} draws={} draw_us={} indices_us={} gpu_index_draws={} gpu_index_bytes={} normalized_indices={}",
        perf.presents, perf.draws, perf.draw_ns / 1000, perf.indices_ns / 1000,
        perf.gpu_index_draws, perf.gpu_index_bytes, perf.normalized_indices);
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
  // DestroyDraw has joined the shared workers. Cancelled jobs have broken
  // promises; failed jobs have already reported their error at the bind.
  const auto destroy_pipeline = [&](PendingPipeline& pending) {
    if (pending.valid()) {
      try {
        if (const auto pipeline = pending.get()) fn.vkDestroyPipeline(vk, pipeline, nullptr);
      } catch (const std::exception&) {}
      pending = {};
    }
  };
  for (auto& pipeline : copy_pipelines_) destroy_pipeline(pipeline);
  for (auto& pipeline : clear_pipelines_) destroy_pipeline(pipeline);
  for (auto& pipeline : load_pipelines_) destroy_pipeline(pipeline);
  if (load_layout_) fn.vkDestroyPipelineLayout(vk, load_layout_, nullptr);
  load_layout_ = VK_NULL_HANDLE;
  if (copy_layout_) fn.vkDestroyPipelineLayout(vk, copy_layout_, nullptr);
  if (clear_layout_) fn.vkDestroyPipelineLayout(vk, clear_layout_, nullptr);
  destroy_pipeline(downscale_pipeline_);
  if (downscale_layout_) fn.vkDestroyPipelineLayout(vk, downscale_layout_, nullptr);
  downscale_layout_=VK_NULL_HANDLE;
  if (storage_layout_) fn.vkDestroyDescriptorSetLayout(vk, storage_layout_, nullptr);
  for (auto& arena : arenas_) {
    if (arena.descriptors) fn.vkDestroyDescriptorPool(vk, arena.descriptors, nullptr);
    arena.descriptors = VK_NULL_HANDLE;
    for (auto pool : arena.extra_descriptor_pools) fn.vkDestroyDescriptorPool(vk, pool, nullptr);
    arena.extra_descriptor_pools.clear();
    arena.uploads.clear();
    arena.load_scratch.clear();
    arena.readback.reset();
    arena.scaled_readbacks.clear();
  }
  copy_layout_ = clear_layout_ = VK_NULL_HANDLE;
  storage_layout_ = VK_NULL_HANDLE;
  readback_ = nullptr;
  edram_buffer_.reset();
  sample_counts_readback_.reset();
  guest_buffer_.reset();
}
void VulkanRender::Begin() {
  if (commands_) return;
  commands_ = submissions_.Begin();
  cmd_.Attach(commands_);
  PublishCompleted();
  auto& arena = arenas_[submissions_.current_slot()];
  if (arena.serial) throw std::logic_error("Native Vulkan: reusing unpublished readback slot");
  if (!arena.readback) arena.readback = std::make_unique<VulkanBuffer>(device_, 16ull << 20,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanBuffer::Access::kReadback);
  readback_ = arena.readback.get();
  CheckVulkan(device_.functions().vkResetDescriptorPool(device_.device(), arena.descriptors, 0),
              "reset render descriptor arena");
  for (auto pool : arena.extra_descriptor_pools)
    CheckVulkan(device_.functions().vkResetDescriptorPool(device_.device(), pool, 0), "reset render descriptor overflow pool");
  arena.descriptor_pool_index = 0;
  arena.descriptor_pool_sets = 0;
  arena.descriptor_count = 0;
  arena.image_descriptors = arena.sampler_descriptors = 0;
  arena.upload_index = 0;
  for (auto& page : arena.uploads) page.used = 0;
  // The pool reset above released the load descriptor sets of this slot.
  for (auto& page : arena.load_scratch) { page.used = 0; page.load_set = VK_NULL_HANDLE; }
  arena.guest_load_set = VK_NULL_HANDLE;
  arena.load_range_sets.clear();
  arena.draw_memory_set = VK_NULL_HANDLE;
}
void VulkanRender::EndRendering() {
  if (!rendering_) return;
  cmd_.EndRenderPass();
  rendering_ = false;
}
// The GPU would otherwise wait for the next guest sync to receive work the
// CPU recorded long before. Submitting only adds a queue boundary: order,
// barriers and guest-visible syncs are unchanged, nothing waits here, and
// pending pipeline creations are never forced (deferred commands stay open).
void VulkanRender::SubmitEarly() {
  if (!eager_threshold_ || !commands_ || recorded_work_ < eager_threshold_ || cmd_.deferred()) return;
  submissions_.Poll();
  // Depth 1 queues the next batch behind the running one (see D3D12Render).
  if (submissions_.last_submitted() - submissions_.completed() > eager_depth_) return;
  Flush(false);
}
void VulkanRender::Flush(bool wait) {
  recorded_work_ = 0;
  if (commands_) {
    EndRendering();
    // Bind pipelines that were still compiling and emit the commands kept
    // behind them, in order, before the command buffer is ended.
    cmd_.Replay();
    auto& arena = arenas_[submissions_.current_slot()];
    if (arena.serial) throw std::logic_error("Native Vulkan: overwriting pending publication");
    const auto serial = submissions_.Submit();
    commands_ = VK_NULL_HANDLE;
    if (!writeback_.empty() || !scaled_readbacks_.empty()) {
      arena.serial = serial;
      std::swap(writeback_, arena.writeback);
      scaled_readbacks_.swap(arena.scaled_readbacks);
      arena.readback_used = std::exchange(readback_used_, 0);
      ++pending_readback_batches_;
    }
  }
  if (wait) submissions_.Drain();
  else if (pending_readback_batches_) submissions_.Poll();
  PublishCompleted();
}
void VulkanRender::PublishCompleted() {
  while (pending_readback_batches_) {
    Arena* arena = nullptr;
    for (auto& candidate : arenas_)
      if (candidate.serial && (!arena || candidate.serial < arena->serial)) arena = &candidate;
    if (!arena || arena->serial > submissions_.completed()) return;
    if (!arena->writeback.empty()) {
      auto& perf = NativeVulkanPerformance();
      VulkanPerformanceTimer timer(perf.readback_ns);
      if (perf.enabled) { ++perf.readbacks; perf.readback_bytes += arena->readback_used; }
      arena->readback->Invalidate(0, arena->readback_used);
      arena->writeback.Complete([&](const GuestWriteback::Write& write) {
        mirror_->CopyReadbackToGuest(write.address,
            reinterpret_cast<const uint8_t*>(arena->readback->mapped() + write.offset), write.length, write.cpu_generations);
      });
    }
    for (const auto& scaled : arena->scaled_readbacks) {
      const size_t bytes=size_t(scaled.ticket.length)*scale_x_*scale_y_;
      scaled.buffer->Invalidate(scaled.offset, bytes);
      scaled_.Complete(scaled.ticket, {reinterpret_cast<const uint8_t*>(scaled.buffer->mapped()+scaled.offset), bytes},
                       scaled.pixel_log2, *memory_);
    }
    arena->scaled_readbacks.clear();
    arena->readback_used = 0;
    arena->serial = 0;
    --pending_readback_batches_;
  }
}
bool VulkanRender::HasGuestWrites() const {
  return pending_readback_batches_ || !writeback_.empty() || !scaled_readbacks_.empty();
}
bool VulkanRender::GuestWritesOverlap(uint32_t address, uint32_t length) const {
  if (!length) return false;
  if (writeback_.Overlaps(address, length)) return true;
  for (const auto& arena : arenas_) if (arena.writeback.Overlaps(address, length)) return true;
  return false;
}
void VulkanRender::CompleteGuestWrites(GuestSync, uint32_t) {
  auto& perf = NativeVulkanPerformance();
  VulkanPerformanceTimer timer(perf.guest_sync_ns);
  if (perf.enabled) ++perf.guest_syncs;
  if (HasGuestWrites()) Flush(true);
}
void VulkanRender::SyncGuestRead(uint32_t address, uint32_t length, uint32_t) {
  auto& perf = NativeVulkanPerformance();
  VulkanPerformanceTimer timer(perf.guest_sync_ns);
  if (perf.enabled) ++perf.guest_syncs;
  if (!length) return;
  if (writeback_.Overlaps(address, length)) Flush(false);
  uint64_t serial = 0;
  for (const auto& arena : arenas_)
    if (arena.writeback.Overlaps(address, length)) serial = std::max(serial, arena.serial);
  if (serial) { submissions_.Wait(serial); PublishCompleted(); }
}
void VulkanRender::ReserveReadback(uint64_t bytes) {
  Begin();
  if (bytes > readback_->size() - readback_used_) { Flush(false); Begin(); }
  if (bytes > readback_->size()) {
    auto& arena = arenas_[submissions_.current_slot()];
    arena.readback = std::make_unique<VulkanBuffer>(device_, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                   VulkanBuffer::Access::kReadback);
    readback_ = arena.readback.get();
  }
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
VkDescriptorPool VulkanRender::CreateDescriptorPool() {
  const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16384},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16384}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 32768},
      {VK_DESCRIPTOR_TYPE_SAMPLER, 32768}, {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 4096}};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.maxSets = 4096;
  pool.poolSizeCount = uint32_t(std::size(sizes));
  pool.pPoolSizes = sizes;
  VkDescriptorPool result = VK_NULL_HANDLE;
  CheckVulkan(device_.functions().vkCreateDescriptorPool(device_.device(), &pool, nullptr, &result),
              "create render descriptor arena");
  return result;
}
VkDescriptorSet VulkanRender::AllocateDescriptors(VkDescriptorSetLayout layout) {
  Begin();
  auto& arena = arenas_[submissions_.current_slot()];
  // A draw normally fits in the caller's reservation, but small storage
  // ranges may require extra immutable load windows. Never flush or reset
  // descriptors in the middle of a draw. Additional pools share this arena's
  // fence and are reset only when the whole slot can be reused.
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorSetCount = 1;
  allocate.pSetLayouts = &layout;
  VkDescriptorSet result = VK_NULL_HANDLE;
  for (;;) {
    allocate.descriptorPool = arena.descriptor_pool_index
        ? arena.extra_descriptor_pools[arena.descriptor_pool_index - 1] : arena.descriptors;
    const auto status = arena.descriptor_pool_sets < 4096
        ? device_.functions().vkAllocateDescriptorSets(device_.device(), &allocate, &result)
        : VK_ERROR_OUT_OF_POOL_MEMORY;
    if (status == VK_SUCCESS) break;
    // Failure in an empty pool cannot be fixed by allocating another equal
    // pool (and must not turn a bad layout or driver failure into an OOM loop).
    if (!arena.descriptor_pool_sets || (status != VK_ERROR_OUT_OF_POOL_MEMORY && status != VK_ERROR_FRAGMENTED_POOL))
      CheckVulkan(status, "allocate render descriptors");
    if (arena.descriptor_pool_index == arena.extra_descriptor_pools.size()) {
      // Reserve before creating the Vulkan object so vector growth cannot
      // leak a successfully-created pool if allocation throws.
      arena.extra_descriptor_pools.reserve(arena.descriptor_pool_index + 1);
      arena.extra_descriptor_pools.push_back(CreateDescriptorPool());
    }
    ++arena.descriptor_pool_index;
    arena.descriptor_pool_sets = 0;
  }
  ++arena.descriptor_count;
  ++arena.descriptor_pool_sets;
  return result;
}
VulkanRender::PendingPipeline VulkanRender::ComputePipeline(VkPipelineLayout layout, std::span<const uint32_t> spirv) {
  // A generated downscale shader is temporary. Copy it before returning and
  // create the module inside the job so cancellation cannot leak a module.
  return pipeline_workers_->Submit<VkPipeline>([this, layout, code = std::vector<uint32_t>(spirv.begin(), spirv.end())] {
    const auto module = CreateNativeShaderModule(device_, code);
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
    if (status != VK_SUCCESS && result)
      device_.functions().vkDestroyPipeline(device_.device(), result, nullptr);
    CheckVulkan(status, "create resolve compute pipeline");
    return result;
  });
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
  // The command thread owns every submitted future, so waiting for them here
  // gives vkMergePipelineCaches exclusive cache access even when shader storage
  // is initialized after a resolve. Ordinary creations remain concurrent.
  const auto wait = [](const PendingPipeline& pipeline) { if (pipeline.valid()) pipeline.wait(); };
  for (const auto& [key, pipeline] : draw_pipelines_) wait(pipeline.pipeline);
  for (const auto& pipeline : load_pipelines_) wait(pipeline);
  for (const auto& pipeline : copy_pipelines_) wait(pipeline);
  for (const auto& pipeline : clear_pipelines_) wait(pipeline);
  wait(downscale_pipeline_);
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
  auto temporary = pipeline_file_;
  temporary += ".tmp";
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream.write(reinterpret_cast<const char*>(bytes.data()), size)) return;
  stream.close();
  // Cache contents are optional and device-specific; a failed replacement must
  // never interfere with guest execution or discard the preceding valid file.
#if defined(_WIN32)
  const bool replaced = MoveFileExW(temporary.c_str(), pipeline_file_.c_str(), MOVEFILE_REPLACE_EXISTING);
#else
  std::filesystem::rename(temporary, pipeline_file_, error);  // rename(2) replaces atomically.
  const bool replaced = !error;
#endif
  if (!replaced) REXLOG_WARN("Native Vulkan: unable to persist pipeline cache");
}
SampleCounts VulkanRender::ReadSampleCounts(bool counting_after) {
  Begin();
  EndRendering();
  // A dedicated host buffer: the counters no longer share the resolve staging
  // buffer, so pending resolve results need not be published (one more full
  // GPU drain) before this snapshot. The single wait below completes them too.
  if (!sample_counts_readback_)
    sample_counts_readback_ = std::make_unique<VulkanBuffer>(device_, sizeof(SampleCounts),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanBuffer::Access::kReadback);
  UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  const VkBufferCopy copy{edram_size_, 0, sizeof(SampleCounts)};
  cmd_.CopyBuffer(edram_buffer_->handle(), sample_counts_readback_->handle(), 1, &copy);
  VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  host.buffer = sample_counts_readback_->handle();
  host.size = sizeof(SampleCounts);
  cmd_.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
      0, 0, nullptr, 1, &host, 0, nullptr);
  Flush(true);
  sample_counts_readback_->Invalidate(0, sizeof(SampleCounts));
  SampleCounts result;
  std::memcpy(result.data(), sample_counts_readback_->mapped(), sizeof(result));
  counting_samples_ = counting_after;
  return result;
}
EdramSnapshot VulkanRender::ReadEdram() {
  const VkDeviceSize bytes = edram_size_ + sizeof(SampleCounts);
  auto readback = std::make_shared<VulkanBuffer>(device_, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 VulkanBuffer::Access::kReadback);
  Begin();
  EndRendering();
  UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  const VkBufferCopy copy{0, 0, bytes};
  cmd_.CopyBuffer(edram_buffer_->handle(), readback->handle(), 1, &copy);
  VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  host.buffer = readback->handle();
  host.size = VK_WHOLE_SIZE;
  cmd_.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
  submissions_.Retain(readback);
  Flush(true);
  readback->Invalidate(0, bytes);
  EdramSnapshot snapshot;
  snapshot.bytes.resize(size_t(bytes));
  std::memcpy(snapshot.bytes.data(), readback->mapped(), size_t(bytes));
  snapshot.sample_counting = counting_samples_;
  return snapshot;
}
void VulkanRender::WriteEdram(const EdramSnapshot& snapshot) {
  const VkDeviceSize bytes = edram_size_ + sizeof(SampleCounts);
  if (snapshot.bytes.size() != bytes)
    throw std::runtime_error("Native Vulkan: EDRAM snapshot size differs (resolution scale?)");
  Begin();
  const auto upload = Upload(snapshot.bytes.data(), bytes, 4);
  EndRendering();
  UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
  const VkBufferCopy copy{upload.offset, 0, bytes};
  cmd_.CopyBuffer(upload.buffer, edram_buffer_->handle(), 1, &copy);
  if (edram_buffer_) edram_draw_layout_.reset();
  counting_samples_ = snapshot.sample_counting;
}
void VulkanRender::SetSampleCounts(const SampleCounts& counts, bool visibility_only) {
  Begin();
  const auto upload = Upload(counts.data(), sizeof(counts), 4);
  EndRendering();
  UseBuffer(edram_buffer_->handle(), edram_access_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
  const VkBufferCopy copy{upload.offset, edram_size_, sizeof(counts)};
  cmd_.CopyBuffer(upload.buffer, edram_buffer_->handle(), 1, &copy);
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
  cmd_.CopyBuffer(upload.buffer, guest_buffer_->handle(), 1, &copy);
}
void VulkanRender::UseBuffer(VkBuffer buffer, BufferAccess& previous,
                             VkPipelineStageFlags stage, VkAccessFlags access, bool force) {
  Begin();
  const auto dependency = previous.Use(stage, access, force);
  if (!dependency) return;
  EndRendering();
  if (buffer == edram_buffer_->handle()) edram_draw_layout_.reset();
  VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  barrier.srcAccessMask = dependency->access;
  barrier.dstAccessMask = access;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = buffer;
  barrier.size = VK_WHOLE_SIZE;
  cmd_.PipelineBarrier(dependency->stages, stage, 0,
                                           0, nullptr, 1, &barrier, 0, nullptr);
}
}
