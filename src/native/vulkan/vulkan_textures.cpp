#include "vulkan_render.h"
#include "../quality/settings.h"
#include "../textures/texture_load.h"
#include "../textures/texture.h"
#include <rex/graphics/pipeline/texture/info.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace x360n::native {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
namespace {
struct Format { VkFormat vk; DXGI_FORMAT conversion; uint32_t bytes, block, components; };
Format TextureFormatFor(const xe_gpu_texture_fetch_t& fetch, bool sign) {
  const auto format = GetBaseFormat(fetch.format);
  if (ExpandedTextureBlock(format)) return {VK_FORMAT_R32G32B32A32_SFLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 1, 4};
  if (const auto components = ExpandedTextureComponents(format, fetch.num_format))
    return {VK_FORMAT_R32G32B32A32_SFLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 1, components};
  switch (format) {
    case TextureFormat::k_2_10_10_10: return sign
        ? Format{VK_FORMAT_R32G32B32A32_SFLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 1, 4}
        : Format{VK_FORMAT_A2B10G10R10_UNORM_PACK32, DXGI_FORMAT_R10G10B10A2_UNORM, 4, 1, 4};
    case TextureFormat::k_4_4_4_4: return sign
        ? Format{VK_FORMAT_R32G32B32A32_SFLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 1, 4}
        : Format{VK_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, 4, 1, 4};
    case TextureFormat::k_8: return {sign ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_UNORM,
        sign ? DXGI_FORMAT_R8_SNORM : DXGI_FORMAT_R8_UNORM, 1, 1, 1};
    case TextureFormat::k_8_8: return {sign ? VK_FORMAT_R8G8_SNORM : VK_FORMAT_R8G8_UNORM,
        sign ? DXGI_FORMAT_R8G8_SNORM : DXGI_FORMAT_R8G8_UNORM, 2, 1, 2};
    case TextureFormat::k_8_8_8_8: return {sign ? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R8G8B8A8_UNORM,
        sign ? DXGI_FORMAT_R8G8B8A8_SNORM : DXGI_FORMAT_R8G8B8A8_UNORM, 4, 1, 4};
    case TextureFormat::k_16: return {sign ? VK_FORMAT_R16_SNORM : VK_FORMAT_R16_UNORM,
        sign ? DXGI_FORMAT_R16_SNORM : DXGI_FORMAT_R16_UNORM, 2, 1, 1};
    case TextureFormat::k_16_16: return {sign ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R16G16_UNORM,
        sign ? DXGI_FORMAT_R16G16_SNORM : DXGI_FORMAT_R16G16_UNORM, 4, 1, 2};
    case TextureFormat::k_16_16_16_16: return {sign ? VK_FORMAT_R16G16B16A16_SNORM : VK_FORMAT_R16G16B16A16_UNORM,
        sign ? DXGI_FORMAT_R16G16B16A16_SNORM : DXGI_FORMAT_R16G16B16A16_UNORM, 8, 1, 4};
    case TextureFormat::k_16_FLOAT: return {VK_FORMAT_R16_SFLOAT, DXGI_FORMAT_R16_FLOAT, 2, 1, 1};
    case TextureFormat::k_16_16_FLOAT: return {VK_FORMAT_R16G16_SFLOAT, DXGI_FORMAT_R16G16_FLOAT, 4, 1, 2};
    case TextureFormat::k_16_16_16_16_FLOAT: return {VK_FORMAT_R16G16B16A16_SFLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, 1, 4};
    case TextureFormat::k_24_8: case TextureFormat::k_24_8_FLOAT:
    case TextureFormat::k_32_FLOAT: return {VK_FORMAT_R32_SFLOAT, DXGI_FORMAT_R32_FLOAT, 4, 1, 1};
    case TextureFormat::k_32_32_FLOAT: return {VK_FORMAT_R32G32_SFLOAT, DXGI_FORMAT_R32G32_FLOAT, 8, 1, 2};
    case TextureFormat::k_32_32_32_32_FLOAT: return {VK_FORMAT_R32G32B32A32_SFLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 1, 4};
    case TextureFormat::k_DXT1: case TextureFormat::k_DXT2_3: case TextureFormat::k_DXT4_5:
    case TextureFormat::k_DXT5A: case TextureFormat::k_DXN:
      if (sign) throw std::runtime_error("Native Vulkan: signed compressed texture is not implemented");
      if (format == TextureFormat::k_DXT1) return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, DXGI_FORMAT_BC1_UNORM, 8, 4, 4};
      if (format == TextureFormat::k_DXT2_3) return {VK_FORMAT_BC2_UNORM_BLOCK, DXGI_FORMAT_BC2_UNORM, 16, 4, 4};
      if (format == TextureFormat::k_DXT4_5) return {VK_FORMAT_BC3_UNORM_BLOCK, DXGI_FORMAT_BC3_UNORM, 16, 4, 4};
      if (format == TextureFormat::k_DXT5A) return {VK_FORMAT_BC4_UNORM_BLOCK, DXGI_FORMAT_BC4_UNORM, 8, 4, 1};
      return {VK_FORMAT_BC5_UNORM_BLOCK, DXGI_FORMAT_BC5_UNORM, 16, 4, 2};
    default: throw std::runtime_error("Native Vulkan: texture format " + std::to_string(uint32_t(format)) + " is not implemented");
  }
}
VkSamplerAddressMode AddressMode(ClampMode mode, bool mirror_clamp) {
  switch (mode) {
    case ClampMode::kRepeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case ClampMode::kMirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case ClampMode::kClampToEdge: case ClampMode::kClampToHalfway: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case ClampMode::kClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case ClampMode::kMirrorClampToEdge: case ClampMode::kMirrorClampToHalfway: case ClampMode::kMirrorClampToBorder:
      if (!mirror_clamp) throw std::runtime_error("Native Vulkan: host lacks mirror-clamp sampler support");
      return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default: throw std::runtime_error("Native Vulkan: invalid sampler address mode");
  }
}
}
struct VulkanRender::TextureResource {
  const Device& device;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceSize bytes = 0;
  TextureResource(const Device& d, VkImageCreateInfo info, VkImageViewType type, VkComponentMapping swizzle)
      : device(d) {
    const auto& fn = device.functions();
    CheckVulkan(fn.vkCreateImage(device.device(), &info, nullptr, &image), "create Native texture");
    try {
      VkMemoryRequirements requirements;
      fn.vkGetImageMemoryRequirements(device.device(), image, &requirements);
      auto types = requirements.memoryTypeBits & device.memory_types().device_local;
      if (!types) types = requirements.memoryTypeBits;
      if (!types) throw std::runtime_error("Native Vulkan: no texture memory type");
      VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocate.allocationSize = bytes = requirements.size;
      allocate.memoryTypeIndex = std::countr_zero(types);
      CheckVulkan(fn.vkAllocateMemory(device.device(), &allocate, nullptr, &memory), "allocate Native texture");
      CheckVulkan(fn.vkBindImageMemory(device.device(), image, memory, 0), "bind Native texture");
      VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view_info.image = image;
      view_info.viewType = type;
      view_info.format = info.format;
      view_info.components = swizzle;
      view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, info.mipLevels, 0, info.arrayLayers};
      CheckVulkan(fn.vkCreateImageView(device.device(), &view_info, nullptr, &view), "create Native texture view");
    } catch (...) {
      fn.vkDestroyImage(device.device(), image, nullptr);
      if (memory) fn.vkFreeMemory(device.device(), memory, nullptr);
      throw;
    }
  }
  ~TextureResource() {
    const auto& fn = device.functions();
    fn.vkDestroyImageView(device.device(), view, nullptr);
    fn.vkDestroyImage(device.device(), image, nullptr);
    fn.vkFreeMemory(device.device(), memory, nullptr);
  }
};
void VulkanRender::TextureWatch(void*, void* data, uint64_t range, bool) {
  auto& entry = *static_cast<TextureEntry*>(data);
  entry.outdated = true;
  entry.watches[range] = nullptr;
}
void VulkanRender::Unwatch(TextureEntry& entry) {
  auto lock = GuestMemoryMirror::Lock();
  for (auto& watch : entry.watches) {
    if (watch) mirror_->Unwatch(watch);
    watch = nullptr;
  }
}
void VulkanRender::DestroyTextures() {
  for (auto& [key, entry] : textures_) Unwatch(entry);
  textures_.clear();
  null_textures_.fill({});
  for (auto& [key, sampler] : samplers_) device_.functions().vkDestroySampler(device_.device(), sampler, nullptr);
  samplers_.clear();
  for (auto& [shader, layout] : texture_layouts_)
    device_.functions().vkDestroyDescriptorSetLayout(device_.device(), layout, nullptr);
  texture_layouts_.clear();
}
VkDescriptorSetLayout VulkanRender::TextureLayout(SpirvShader* shader) {
  if (auto found = texture_layouts_.find(shader); found != texture_layouts_.end()) return found->second;
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  if (shader) {
    const auto stage = shader->type() == ShaderType::kVertex ? VkShaderStageFlags(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) : VkShaderStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT);
    const auto images = shader->GetTextureBindingsAfterTranslation().size();
    const auto samplers = shader->GetSamplerBindingsAfterTranslation().size();
    for (uint32_t i = 0; i < images + samplers; ++i)
      bindings.push_back({i, i < images ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER,
                          1, VkShaderStageFlags(stage), nullptr});
  }
  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  info.bindingCount = uint32_t(bindings.size());
  info.pBindings = bindings.data();
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  CheckVulkan(device_.functions().vkCreateDescriptorSetLayout(device_.device(), &info, nullptr, &layout),
              "create Native texture descriptor layout");
  texture_layouts_.emplace(shader, layout);
  return layout;
}
void VulkanRender::SyncTextureReads(const RegisterFile& regs, SpirvShader* shader) {
  if (!shader || writeback_.empty()) return;
  for (const auto& binding : shader->GetTextureBindingsAfterTranslation()) {
    const auto fetch = regs.GetTextureFetch(binding.fetch_constant);
    if (!TextureViewActive(fetch, binding.dimension, binding.is_signed)) continue;
    uint32_t w, h, d, base, mip, low, high;
    texture_util::GetSubresourcesFromFetchConstant(fetch, &w, &h, &d, &base, &mip, &low, &high);
    const auto layout = texture_util::GetGuestTextureLayout(fetch.dimension, fetch.pitch,
        w + 1, h + 1, d + 1, fetch.tiled, fetch.format, fetch.packed_mips, base != 0, high);
    if (base) SyncGuestRead(base << 12, (layout.base.level_data_extent_bytes + 3) & ~3u, 0xFF);
    if (mip && high) SyncGuestRead(mip << 12, (layout.mips_total_extent_bytes + 3) & ~3u, 0xFF);
  }
}
VkDescriptorSet VulkanRender::BindTextures(const RegisterFile& regs, rex::memory::Memory& memory, SpirvShader* shader) {
  const auto set = AllocateDescriptors(TextureLayout(shader));
  if (!shader) return set;
  const auto& fn = device_.functions();
  const auto& bindings = shader->GetTextureBindingsAfterTranslation();
  const auto& sampler_bindings = shader->GetSamplerBindingsAfterTranslation();
  auto& arena = arenas_[submissions_.current_slot()];
  arena.image_descriptors += uint32_t(bindings.size());
  arena.sampler_descriptors += uint32_t(sampler_bindings.size());
  std::vector<VkDescriptorImageInfo> infos(bindings.size() + sampler_bindings.size());
  std::vector<VkWriteDescriptorSet> writes(infos.size());
  auto transition = [&](VkImage image, uint32_t levels, uint32_t layers, bool ready) {
    EndRendering();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = ready ? VK_ACCESS_TRANSFER_WRITE_BIT : 0;
    barrier.dstAccessMask = ready ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = ready ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, layers};
    fn.vkCmdPipelineBarrier(commands_, ready ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        ready ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
  };
  for (size_t i = 0; i < bindings.size(); ++i) {
    const auto& binding = bindings[i];
    const auto fetch = regs.GetTextureFetch(binding.fetch_constant);
    uint32_t w = 0, h = 0, d = 0, base = 0, mip = 0, low = 0, high = 0;
    bool active = TextureViewActive(fetch, binding.dimension, binding.is_signed);
    if (active) texture_util::GetSubresourcesFromFetchConstant(fetch, &w, &h, &d, &base, &mip, &low, &high);
    active &= base || mip;
    // The console's 1D encoding is linear and unpacked. Invalid 1D fetches
    // produce the same zero view used by the shared Native texture contract.
    if (fetch.dimension == DataDimension::k1D && (w >= kTexture2DCubeMaxWidthHeight || fetch.tiled || fetch.packed_mips))
      active = false;
    std::shared_ptr<TextureResource> resource;
    if (!active) {
      const unsigned kind = binding.dimension == FetchOpDimension::kCube ? 2 :
                            binding.dimension == FetchOpDimension::k3DOrStacked ? 1 : 0;
      auto& null = null_textures_[kind];
      if (!null) {
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.flags = kind == 2 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        image.imageType = kind == 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        image.format = VK_FORMAT_R8G8B8A8_UNORM;
        image.extent = {1, 1, 1};
        image.mipLevels = 1;
        image.arrayLayers = kind == 2 ? 6 : 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        null = std::make_shared<TextureResource>(device_, image, kind == 2 ? VK_IMAGE_VIEW_TYPE_CUBE :
            kind == 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D_ARRAY, VkComponentMapping{});
        transition(null->image, 1, image.arrayLayers, false);
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, image.arrayLayers};
        fn.vkCmdClearColorImage(commands_, null->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        transition(null->image, 1, image.arrayLayers, true);
      }
      resource = null;
    } else {
      if (fetch.border_size || (fetch.dimension == DataDimension::k3D && fetch.stacked))
        throw std::runtime_error("Native Vulkan: unsupported texture border/stacked volume storage");
      const bool volume = fetch.dimension == DataDimension::k3D && binding.dimension == FetchOpDimension::k3DOrStacked;
      if (fetch.dimension == DataDimension::k3D && !volume) high = base ? 0 : low;
      const bool cube = fetch.dimension == DataDimension::kCube;
      const unsigned layers = cube || (fetch.dimension == DataDimension::k2DOrStacked && fetch.stacked) ? d + 1 : 1;
      const unsigned levels = high + 1;
      const auto format = TextureFormatFor(fetch, binding.is_signed);
      TextureLoadInput input{fetch.format, fetch.dimension, fetch.endianness, bool(fetch.tiled), bool(fetch.packed_mips),
          w + 1, h + 1, d + 1, fetch.pitch, base << 12, mip << 12, high, volume, layers,
          format.conversion, bool(binding.is_signed), bool(fetch.num_format)};
      const auto layout = input.Layout();
      const uint32_t base_bytes = base ? (layout.base.level_data_extent_bytes + 3) & ~3u : 0;
      const uint32_t mip_bytes = mip && high ? (layout.mips_total_extent_bytes + 3) & ~3u : 0;
      if (uint64_t(input.base) + base_bytes > GuestMemoryMirror::kSize ||
          uint64_t(input.mip) + mip_bytes > GuestMemoryMirror::kSize)
        throw std::runtime_error("Native Vulkan: texture outside physical memory");
      const auto* scale_format = FormatInfo::Get(input.format);
      const bool scaled_texture = scaled_.enabled() && !input.host_volume &&
          scale_format->block_width == 1 && scale_format->block_height == 1 &&
          (scaled_.Overlaps(input.base, base_bytes) || scaled_.Overlaps(input.mip, mip_bytes));
      if (scaled_texture) { input.scale_x=scale_x_; input.scale_y=scale_y_; input.scaled=&scaled_;
        textures_resolution_scaled_ |= uint32_t(1) << binding.fetch_constant; }
      const uint32_t key_data[]{input.scale_x, input.scale_y, base, mip, w, h, d, levels, fetch.pitch, uint32_t(fetch.format), fetch.swizzle,
          uint32_t(fetch.tiled) | uint32_t(fetch.packed_mips) << 1 | uint32_t(fetch.endianness) << 2 |
          uint32_t(fetch.dimension) << 4 | uint32_t(fetch.stacked) << 6 | uint32_t(binding.is_signed) << 7 |
          uint32_t(volume) << 8 | uint32_t(fetch.num_format) << 9};
      const std::string key(reinterpret_cast<const char*>(key_data), sizeof(key_data));
      auto found = textures_.find(key);
      bool current = false;
      if (found != textures_.end()) {
        auto lock = GuestMemoryMirror::Lock();
        current = !found->second.outdated;
      }
      if (current) {
        resource = found->second.resource;
        found->second.last_use = ++texture_use_;
      } else {
        // These reads were synchronized before any descriptor sets of the draw
        // were allocated, so a resolve cannot strand half-recorded draw state.
        if (writeback_.Overlaps(input.base, base_bytes) || writeback_.Overlaps(input.mip, mip_bytes))
          throw std::logic_error("Native Vulkan: CPU texture conversion overlaps pending resolve");
        if (found != textures_.end()) {
          Unwatch(found->second);
          submissions_.Retain(found->second.resource);
          texture_bytes_ -= found->second.resource->bytes;
          textures_.erase(found);
        }
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        image.imageType = volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        image.format = format.vk;
        image.extent = {(w + 1)*input.scale_x, (h + 1)*input.scale_y, volume ? d + 1 : 1};
        if (image.extent.width>device_.properties().maxImageDimension2D || image.extent.height>device_.properties().maxImageDimension2D)
          throw std::runtime_error("Native: scaled texture exceeds Vulkan image dimensions");
        image.mipLevels = levels;
        image.arrayLayers = layers;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        auto component = [&](unsigned channel) {
          const uint32_t value = uint32_t(fetch.swizzle >> (channel * 3)) & 7u;
          if (value > 5) throw std::runtime_error("Native Vulkan: reserved texture swizzle");
          return value == 4 ? VK_COMPONENT_SWIZZLE_ZERO : value == 5 ? VK_COMPONENT_SWIZZLE_ONE :
              VkComponentSwizzle(uint32_t(VK_COMPONENT_SWIZZLE_R) + std::min(value, format.components - 1));
        };
        resource = std::make_shared<TextureResource>(device_, image, cube ? VK_IMAGE_VIEW_TYPE_CUBE :
            volume ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            VkComponentMapping{component(0), component(1), component(2), component(3)});
        constexpr uint64_t kBudget = 1ull << 30;
        while (!textures_.empty() && (textures_.size() >= 4096 || texture_bytes_ + resource->bytes > kBudget)) {
          auto oldest = std::min_element(textures_.begin(), textures_.end(),
              [](const auto& a, const auto& b) { return a.second.last_use < b.second.last_use; });
          Unwatch(oldest->second);
          submissions_.Retain(oldest->second.resource);
          texture_bytes_ -= oldest->second.resource->bytes;
          textures_.erase(oldest);
        }
        auto& entry = textures_.emplace(key, TextureEntry{resource, false, ++texture_use_}).first->second;
        texture_bytes_ += resource->bytes;
        {
          auto lock = GuestMemoryMirror::Lock();
          if (base_bytes) entry.watches[0] = mirror_->Watch(input.base, base_bytes, TextureWatch, this, &entry, 0);
          if (mip_bytes) entry.watches[1] = mirror_->Watch(input.mip, mip_bytes, TextureWatch, this, &entry, 1);
        }
        // RequestRange arms physical write faults as well as making the mirror
        // current, so subsequent CPU writes invalidate the resident texture.
        if (base_bytes) mirror_->RequestRange(input.base, base_bytes);
        if (mip_bytes) mirror_->RequestRange(input.mip, mip_bytes);
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(layers * levels);
        std::vector<VkBufferImageCopy> copies;
        uint64_t bytes = 0;
        for (unsigned layer = 0; layer < layers; ++layer) for (unsigned level = 0; level < levels; ++level) {
          auto& footprint = footprints[layer * levels + level];
          const unsigned width = std::max(1u, ((w + 1)*input.scale_x) >> level), height = std::max(1u, ((h + 1)*input.scale_y) >> level);
          const unsigned depth = volume ? std::max(1u, (d + 1) >> level) : 1;
          const unsigned row = (((width + format.block - 1) / format.block * format.bytes + 255) / 256) * 256;
          bytes = (bytes + 511) & ~uint64_t(511);
          footprint.Offset = bytes;
          footprint.Footprint = {format.conversion, width, height, depth, row};
          VkBufferImageCopy copy{};
          copy.bufferOffset = bytes;
          copy.bufferRowLength = row / format.bytes * format.block;
          copy.bufferImageHeight = ((height + format.block - 1) / format.block) * format.block;
          copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1};
          copy.imageExtent = {width, height, depth};
          if (base || level) copies.push_back(copy);
          bytes += uint64_t(row) * ((height + format.block - 1) / format.block) * depth;
        }
        std::vector<uint8_t> linear(size_t(bytes), 0);
        const std::span<const uint8_t> base_source(base_bytes ? memory.TranslatePhysical<const uint8_t*>(input.base) : nullptr, base_bytes);
        const std::span<const uint8_t> mip_source(mip_bytes ? memory.TranslatePhysical<const uint8_t*>(input.mip) : nullptr, mip_bytes);
        ConvertTextureCpu(input, base_source, mip_source, footprints, linear.data());
        const auto upload = Upload(linear.data(), bytes, 512);
        for (auto& copy : copies) copy.bufferOffset += upload.offset;
        transition(resource->image, levels, layers, false);
        fn.vkCmdCopyBufferToImage(commands_, upload.buffer, resource->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  uint32_t(copies.size()), copies.data());
        transition(resource->image, levels, layers, true);
      }
    }
    infos[i].imageView = resource->view;
    infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    submissions_.Retain(resource);
  }
  for (size_t i = 0; i < sampler_bindings.size(); ++i) {
    const auto& binding = sampler_bindings[i];
    const auto fetch = regs.GetTextureFetch(binding.fetch_constant);
    const auto mag = binding.mag_filter == TextureFilter::kUseFetchConst ? fetch.mag_filter : binding.mag_filter;
    const auto min = binding.min_filter == TextureFilter::kUseFetchConst ? fetch.min_filter : binding.min_filter;
    const auto mip = binding.mip_filter == TextureFilter::kUseFetchConst ? fetch.mip_filter : binding.mip_filter;
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = mag == TextureFilter::kLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = min == TextureFilter::kLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = mip == TextureFilter::kLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ClampMode cx, cy, cz;
    texture_util::GetClampModesForDimension(fetch, cx, cy, cz);
    info.addressModeU = AddressMode(cx, device_.properties().samplerMirrorClampToEdge);
    info.addressModeV = AddressMode(cy, device_.properties().samplerMirrorClampToEdge);
    info.addressModeW = AddressMode(cz, device_.properties().samplerMirrorClampToEdge);
    const auto aniso = NativeAnisotropy(std::min(binding.aniso_filter == AnisoFilter::kUseFetchConst ? fetch.aniso_filter : binding.aniso_filter,
                               AnisoFilter::kMax_16_1));
    info.maxAnisotropy = 1;
    if (aniso != AnisoFilter::kDisabled) {
      if (!device_.properties().samplerAnisotropy) throw std::runtime_error("Native Vulkan: anisotropy unavailable");
      info.anisotropyEnable = VK_TRUE;
      info.maxAnisotropy = std::min(float(1u << (uint32_t(aniso) - 1)), device_.properties().maxSamplerAnisotropy);
      info.magFilter = info.minFilter = VK_FILTER_LINEAR;
      info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    uint32_t low, high;
    texture_util::GetSubresourcesFromFetchConstant(fetch, nullptr, nullptr, nullptr, nullptr, nullptr, &low, &high);
    info.minLod = float(low);
    info.maxLod = mip == TextureFilter::kBaseMap ? info.minLod + 0.25f : float(high);
    const bool border = info.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        info.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER || info.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.borderColor = border && fetch.border_color == BorderColor::k_ABGR_White
        ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (border && (fetch.border_color == BorderColor::k_ACBYCR_Black || fetch.border_color == BorderColor::k_ACBCRY_Black))
      throw std::runtime_error("Native Vulkan: YCbCr sampler border color is not implemented");
    const std::string key(reinterpret_cast<const char*>(&info), sizeof(info));
    auto [it, created] = samplers_.try_emplace(key, VK_NULL_HANDLE);
    if (created) CheckVulkan(fn.vkCreateSampler(device_.device(), &info, nullptr, &it->second), "create Native texture sampler");
    infos[bindings.size() + i].sampler = it->second;
  }
  for (size_t i = 0; i < writes.size(); ++i) {
    auto& write = writes[i];
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = uint32_t(i);
    write.descriptorCount = 1;
    write.descriptorType = i < bindings.size() ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &infos[i];
  }
  fn.vkUpdateDescriptorSets(device_.device(), uint32_t(writes.size()), writes.data(), 0, nullptr);
  return set;
}
}
