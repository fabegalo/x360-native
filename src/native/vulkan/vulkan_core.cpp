#include "vulkan_core.h"
#include <rex/logging.h>
#include <rex/cvar.h>
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>
#include <algorithm>
#include <bit>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
REXCVAR_DEFINE_BOOL(native_vulkan_perf, false, "GPU/Native", "Measure Native Vulkan submission, fence, buffer and publication costs");

namespace x360n::native {
VulkanPerformance& NativeVulkanPerformance() {
  thread_local VulkanPerformance counters{REXCVAR_GET(native_vulkan_perf)};
  return counters;
}
void CheckVulkan(VkResult result, const char* operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string("Native Vulkan: ") + operation +
                             " VkResult=" + std::to_string(int(result)));
}

VulkanBuffer::VulkanBuffer(const rex::ui::vulkan::VulkanDevice& device,
                           VkDeviceSize size, VkBufferUsageFlags usage, Access access)
    : device_(device), size_(size) {
  if (!size) throw std::invalid_argument("Native Vulkan: zero buffer size");
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  CheckVulkan(fn.vkCreateBuffer(vk, &info, nullptr, &buffer_), "create buffer");
  try {
    VkMemoryRequirements requirements{};
    fn.vkGetBufferMemoryRequirements(vk, buffer_, &requirements);
    const auto& types = device_.memory_types();
    uint32_t allowed = requirements.memoryTypeBits;
    uint32_t preferred;
    if (access == Access::kDevice) {
      preferred = allowed & types.device_local;
    } else {
      allowed &= types.host_visible;
      preferred = allowed & (access == Access::kReadback ? types.host_cached : types.host_coherent);
    }
    if (!allowed) throw std::runtime_error("Native Vulkan: no compatible buffer memory type");
    const uint32_t type = std::countr_zero(preferred ? preferred : allowed);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    CheckVulkan(fn.vkAllocateMemory(vk, &allocate, nullptr, &memory_), "allocate buffer memory");
    allocation_size_ = requirements.size;
    CheckVulkan(fn.vkBindBufferMemory(vk, buffer_, memory_, 0), "bind buffer memory");
    coherent_ = (types.host_coherent & (uint32_t(1) << type)) != 0;
    if (access != Access::kDevice) {
      void* mapping = nullptr;
      CheckVulkan(fn.vkMapMemory(vk, memory_, 0, VK_WHOLE_SIZE, 0, &mapping), "map buffer memory");
      mapped_ = static_cast<std::byte*>(mapping);
    }
    auto& perf = NativeVulkanPerformance();
    if (perf.enabled) {
      measured_ = true;
      ++perf.buffers;
      perf.allocated_bytes += allocation_size_;
      perf.live_bytes += allocation_size_;
      perf.peak_bytes = std::max(perf.peak_bytes, perf.live_bytes);
    }
  } catch (...) {
    fn.vkDestroyBuffer(vk, buffer_, nullptr);
    if (memory_) fn.vkFreeMemory(vk, memory_, nullptr);
    throw;
  }
}

VulkanBuffer::~VulkanBuffer() {
  if (measured_) NativeVulkanPerformance().live_bytes -= allocation_size_;
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  if (mapped_) fn.vkUnmapMemory(vk, memory_);
  fn.vkDestroyBuffer(vk, buffer_, nullptr);
  fn.vkFreeMemory(vk, memory_, nullptr);
}

void VulkanBuffer::SynchronizeMapping(VkDeviceSize offset, VkDeviceSize size, bool invalidate) {
  if (!mapped_ || offset > size_ || size > size_ - offset)
    throw std::out_of_range("Native Vulkan: mapped buffer range is invalid");
  if (!size || coherent_) return;
  const auto atom = device_.properties().nonCoherentAtomSize;
  const auto begin = offset - offset % atom;
  const auto end = std::min(allocation_size_, ((offset + size - 1) / atom + 1) * atom);
  VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
  range.memory = memory_;
  range.offset = begin;
  range.size = end - begin;
  const auto& fn = device_.functions();
  CheckVulkan(invalidate ? fn.vkInvalidateMappedMemoryRanges(device_.device(), 1, &range)
                          : fn.vkFlushMappedMemoryRanges(device_.device(), 1, &range),
              invalidate ? "invalidate buffer mapping" : "flush buffer mapping");
}
void VulkanBuffer::Flush(VkDeviceSize offset, VkDeviceSize size) {
  SynchronizeMapping(offset, size, false);
}
void VulkanBuffer::Invalidate(VkDeviceSize offset, VkDeviceSize size) {
  SynchronizeMapping(offset, size, true);
}

VulkanSubmissionRing::VulkanSubmissionRing(const rex::ui::vulkan::VulkanDevice& device)
    : device_(device) {
  const auto& fn = device_.functions();
  const auto vk = device_.device();
  try {
    if (NativeVulkanPerformance().enabled) {
      VkPhysicalDeviceProperties properties;
      device_.vulkan_instance()->functions().vkGetPhysicalDeviceProperties(device_.physical_device(), &properties);
      timestamp_period_ = properties.limits.timestampPeriod;
      uint32_t count = 0;
      const auto get_families = device_.vulkan_instance()->functions().vkGetPhysicalDeviceQueueFamilyProperties;
      get_families(device_.physical_device(), &count, nullptr);
      std::vector<VkQueueFamilyProperties> families(count);
      get_families(device_.physical_device(), &count, families.data());
      timestamp_bits_ = families.at(device_.queue_family_graphics_compute()).timestampValidBits;
      if (timestamp_bits_) {
        VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        query.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query.queryCount = uint32_t(kSlots * 2);
        CheckVulkan(LoadNativeVulkanFunction<PFN_vkCreateQueryPool>(device_, "vkCreateQueryPool")(
            vk, &query, nullptr, &timestamps_), "create performance timestamps");
      }
    }
    for (auto& slot : slots_) {
      VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
      pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
      pool.queueFamilyIndex = device_.queue_family_graphics_compute();
      CheckVulkan(fn.vkCreateCommandPool(vk, &pool, nullptr, &slot.pool), "create command pool");
      VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      allocate.commandPool = slot.pool;
      allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocate.commandBufferCount = 1;
      CheckVulkan(fn.vkAllocateCommandBuffers(vk, &allocate, &slot.commands), "allocate command buffer");
      VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      CheckVulkan(fn.vkCreateFence(vk, &fence, nullptr, &slot.fence), "create submission fence");
    }
  } catch (...) { Destroy(); throw; }
}
VulkanSubmissionRing::~VulkanSubmissionRing() {
  // A timeout is a backend failure, not permission to free live GPU resources.
  try { Drain(); } catch (const std::exception& error) {
    REX_FATAL("Native Vulkan: unsafe submission teardown: {}", error.what());
  }
  Destroy();
}
void VulkanSubmissionRing::Destroy() {
  const auto& fn = device_.functions();
  if (timestamps_) LoadNativeVulkanFunction<PFN_vkDestroyQueryPool>(device_, "vkDestroyQueryPool")(
      device_.device(), timestamps_, nullptr);
  timestamps_ = VK_NULL_HANDLE;
  for (auto& slot : slots_) {
    slot.retained.clear();
    if (slot.pool) fn.vkDestroyCommandPool(device_.device(), slot.pool, nullptr);
    if (slot.fence) fn.vkDestroyFence(device_.device(), slot.fence, nullptr);
    slot.pool = VK_NULL_HANDLE;
    slot.fence = VK_NULL_HANDLE;
  }
}
VkCommandBuffer VulkanSubmissionRing::Begin() {
  if (recording_) throw std::logic_error("Native Vulkan: submission is already recording");
  auto& slot = slots_[current_slot()];
  Wait(slot.serial);
  slot.retained.clear();
  const auto& fn = device_.functions();
  CheckVulkan(fn.vkResetCommandPool(device_.device(), slot.pool, 0), "reset command pool");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  CheckVulkan(fn.vkBeginCommandBuffer(slot.commands, &begin), "begin command buffer");
  if (timestamps_) {
    const auto query = uint32_t(current_slot() * 2);
    LoadNativeVulkanFunction<PFN_vkCmdResetQueryPool>(device_, "vkCmdResetQueryPool")(slot.commands, timestamps_, query, 2);
    LoadNativeVulkanFunction<PFN_vkCmdWriteTimestamp>(device_, "vkCmdWriteTimestamp")(
        slot.commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps_, query);
  }
  recording_ = true;
  return slot.commands;
}
void VulkanSubmissionRing::RetainResource(std::shared_ptr<void> resource) {
  if (!recording_) throw std::logic_error("Native Vulkan: retaining resource without recording");
  slots_[current_slot()].retained.push_back(std::move(resource));
}
uint64_t VulkanSubmissionRing::Submit() {
  auto& perf = NativeVulkanPerformance();
  VulkanPerformanceTimer timer(perf.submit_ns);
  if (!recording_) throw std::logic_error("Native Vulkan: no command buffer to submit");
  if (serial_ == std::numeric_limits<uint64_t>::max())
    throw std::overflow_error("Native Vulkan: submission serial overflow");
  auto& slot = slots_[current_slot()];
  const auto& fn = device_.functions();
  if (timestamps_) LoadNativeVulkanFunction<PFN_vkCmdWriteTimestamp>(device_, "vkCmdWriteTimestamp")(
      slot.commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, uint32_t(current_slot() * 2 + 1));
  CheckVulkan(fn.vkEndCommandBuffer(slot.commands), "end command buffer");
  CheckVulkan(fn.vkResetFences(device_.device(), 1, &slot.fence), "reset submission fence");
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &slot.commands;
  {
    // Presenter uses queue 0 too. The provider owns its external-synchronization
    // lock so Native submissions cannot race window painting or capture.
    auto queue = device_.AcquireQueue(device_.queue_family_graphics_compute(), 0);
    CheckVulkan(fn.vkQueueSubmit(queue.queue(), 1, &submit, slot.fence), "submit queue");
  }
  recording_ = false;
  slot.serial = ++serial_;
  slot.timed = timestamps_ != VK_NULL_HANDLE;
  if (perf.enabled) ++perf.submissions;
  return serial_;
}
void VulkanSubmissionRing::Wait(uint64_t serial) {
  if (serial <= completed_) return;
  if (serial > serial_) throw std::out_of_range("Native Vulkan: waiting for an unsubmitted serial");
  auto& slot = slots_[size_t((serial - 1) % kSlots)];
  // A slot cannot be overwritten without waiting for its old serial.
  if (slot.serial != serial) throw std::logic_error("Native Vulkan: missing submission fence");
  auto& perf = NativeVulkanPerformance();
  VulkanPerformanceTimer timer(perf.wait_ns);
  if (perf.enabled) {
    ++perf.waits;
    const auto status = device_.functions().vkGetFenceStatus(device_.device(), slot.fence);
    if (status == VK_NOT_READY) ++perf.blocked_waits;
    else CheckVulkan(status, "measure submission fence");
  }
  CheckVulkan(device_.functions().vkWaitForFences(device_.device(), 1, &slot.fence,
                                                VK_TRUE, 5'000'000'000ull),
              "GPU fence (5 second limit)");
  completed_ = serial;
  MeasureCompleted();
}
void VulkanSubmissionRing::MeasureCompleted() {
  if (!timestamps_) return;
  auto& perf = NativeVulkanPerformance();
  for (size_t i = 0; i < kSlots; ++i) {
    auto& slot = slots_[i];
    if (!slot.timed || !slot.serial || slot.serial > completed_) continue;
    uint64_t ticks[2];
    CheckVulkan(LoadNativeVulkanFunction<PFN_vkGetQueryPoolResults>(device_, "vkGetQueryPoolResults")(
        device_.device(), timestamps_, uint32_t(i * 2), 2, sizeof(ticks), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT),
        "read performance timestamps");
    const uint64_t mask = timestamp_bits_ == 64 ? UINT64_MAX : (uint64_t(1) << timestamp_bits_) - 1;
    perf.gpu_ns += uint64_t(double((ticks[1] - ticks[0]) & mask) * timestamp_period_);
    ++perf.gpu_samples;
    slot.timed = false;
  }
}
void VulkanSubmissionRing::Poll() {
  for (uint64_t serial = serial_; serial > completed_; --serial) {
    const auto& slot = slots_[size_t((serial - 1) % kSlots)];
    const auto status = device_.functions().vkGetFenceStatus(device_.device(), slot.fence);
    if (status == VK_NOT_READY) continue;
    CheckVulkan(status, "poll submission fence");
    completed_ = serial;
    MeasureCompleted();
    break;
  }
}
void VulkanSubmissionRing::Drain() {
  if (NativeVulkanPerformance().enabled) ++NativeVulkanPerformance().drains;
  Wait(serial_);
}

std::vector<uint32_t> CompileNativeGlsl(VkShaderStageFlagBits stage, std::string_view source) {
  EShLanguage language;
  switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT: language = EShLangVertex; break;
    case VK_SHADER_STAGE_FRAGMENT_BIT: language = EShLangFragment; break;
    case VK_SHADER_STAGE_COMPUTE_BIT: language = EShLangCompute; break;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: language = EShLangTessControl; break;
    case VK_SHADER_STAGE_GEOMETRY_BIT: language = EShLangGeometry; break;
    default: throw std::invalid_argument("Native Vulkan: unsupported host shader stage");
  }
  if (source.empty() || source.size() > size_t(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Native Vulkan: invalid host shader source length");
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    if (!glslang::InitializeProcess()) throw std::runtime_error("Native Vulkan: initialize glslang failed");
  });
  // Native host stages use GLSL; guest shaders use the shared translator.
  // Set explicit portable
  // resource limits instead of depending on an optional glslang executable.
  TBuiltInResource limits{};
  limits.maxVertexAttribs = 16;
  limits.maxVertexUniformComponents = limits.maxFragmentUniformComponents = 1024;
  limits.maxVertexOutputVectors = limits.maxFragmentInputVectors = 16;
  limits.maxVaryingFloats = limits.maxVaryingComponents = 64;
  limits.maxVertexOutputComponents = limits.maxFragmentInputComponents = 64;
  limits.maxGeometryInputComponents = limits.maxGeometryOutputComponents = 128;
  limits.maxGeometryOutputVertices = 256;
  limits.maxGeometryTotalOutputComponents = 1024;
  limits.maxGeometryUniformComponents = 1024;
  limits.maxClipDistances = limits.maxCullDistances = limits.maxCombinedClipAndCullDistances = 8;
  limits.maxPatchVertices = 32;
  limits.maxTessGenLevel = 64;
  limits.maxTessControlInputComponents = limits.maxTessControlOutputComponents = 128;
  limits.maxTessControlTotalOutputComponents = 4096;
  limits.maxTessPatchComponents = 120;
  limits.maxTessControlUniformComponents = 1024;
  limits.maxDrawBuffers = 8;
  limits.maxTextureImageUnits = limits.maxCombinedTextureImageUnits = 16;
  limits.maxImageUnits = limits.maxFragmentImageUniforms = limits.maxComputeImageUniforms = 8;
  limits.maxCombinedImageUniforms = limits.maxCombinedShaderOutputResources = 8;
  limits.maxComputeWorkGroupCountX = limits.maxComputeWorkGroupCountY = limits.maxComputeWorkGroupCountZ = 65535;
  limits.maxComputeWorkGroupSizeX = limits.maxComputeWorkGroupSizeY = 1024;
  limits.maxComputeWorkGroupSizeZ = 64;
  limits.maxComputeUniformComponents = 1024;
  limits.maxComputeTextureImageUnits = 16;
  limits.limits = {true, true, true, true, true, true, true, true, true};
  glslang::TShader shader(language);
  const char* text = source.data();
  const int length = int(source.size());
  shader.setStringsWithLengths(&text, &length, 1);
  shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 450);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
  constexpr auto messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
  if (!shader.parse(&limits, 450, false, messages))
    throw std::runtime_error(std::string("Native Vulkan: host shader parse failed: ") + shader.getInfoLog());
  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(messages))
    throw std::runtime_error(std::string("Native Vulkan: host shader link failed: ") + program.getInfoLog());
  const auto* intermediate = program.getIntermediate(language);
  if (!intermediate) throw std::runtime_error("Native Vulkan: missing host shader intermediate");
  glslang::SpvOptions options{};
  options.disableOptimizer = true;
  std::vector<uint32_t> spirv;
  glslang::GlslangToSpv(*intermediate, spirv, &options);
  if (spirv.empty()) throw std::runtime_error("Native Vulkan: empty host shader SPIR-V");
  return spirv;
}
VkShaderModule CreateNativeShaderModule(const rex::ui::vulkan::VulkanDevice& device,
                                        std::span<const uint32_t> spirv) {
  if (spirv.size() < 5 || spirv[0] != 0x07230203u)
    throw std::invalid_argument("Native Vulkan: invalid SPIR-V module");
  const auto dump_directory = rex::cvar::GetFlagByName("dump_shaders");
  uint64_t diagnostic_id = 0;
  if (!dump_directory.empty()) {
    static std::atomic<uint64_t> sequence{0};
    diagnostic_id = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto directory = std::filesystem::path(dump_directory);
    std::filesystem::create_directories(directory);
    const auto path = directory / ("native-module-" + std::to_string(diagnostic_id) + ".spv");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(spirv.data()), spirv.size_bytes());
    out.close();
    if (!out) throw std::runtime_error("Native Vulkan: could not write requested shader diagnostic");
    REXLOG_INFO("Native Vulkan module begin id={} words={} path={}", diagnostic_id, spirv.size(), path.string());
  }
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = spirv.size_bytes();
  info.pCode = spirv.data();
  VkShaderModule result = VK_NULL_HANDLE;
  CheckVulkan(device.functions().vkCreateShaderModule(device.device(), &info, nullptr, &result),
              "create shader module");
  if (diagnostic_id) REXLOG_INFO("Native Vulkan module end id={} module={:016X}", diagnostic_id, uint64_t(result));
  return result;
}
}
