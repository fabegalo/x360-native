// Headless synthetic GPU clear/transition smoke. No guest, window or game.
// This does not certify rasterized coverage, alpha-to-coverage or gameplay.
#include <rex/graphics/util/draw.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>
#include <iostream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include <d3d12.h>
#include <wrl/client.h>
#include <windows.h>
#endif

using rex::graphics::xenos::MsaaSamples;
void Need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void Vulkan(bool xenos_features) {
  using namespace rex::ui::vulkan;
  auto instance = VulkanInstance::Create(false, true);
  if (!instance) { std::cout << "Vulkan unavailable\n"; return; }
  std::vector<VkPhysicalDevice> physical;
  instance->EnumeratePhysicalDevices(physical);
  Need(!physical.empty(), "no Vulkan physical device");
  auto device = VulkanDevice::CreateIfSupported(instance.get(), physical[0], xenos_features, false);
  Need(bool(device), "Vulkan device creation");
  const auto& fn = device->functions(); const auto& ifn = instance->functions();
  const auto vk = device->device();
  auto image_format_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(
      ifn.vkGetInstanceProcAddr(instance->instance(), "vkGetPhysicalDeviceImageFormatProperties"));
  Need(image_format_properties != nullptr, "image format query");
  std::cout << "Vulkan GPU=" << device->properties().deviceName << '\n';
  std::cout << "Vulkan Xenos features=" << xenos_features << " sample locations extension=" << device->extensions().ext_EXT_sample_locations
            << " variable=" << device->properties().variableSampleLocations << '\n';
  VkPhysicalDeviceMemoryProperties memory;
  ifn.vkGetPhysicalDeviceMemoryProperties(physical[0], &memory);
  for (unsigned samples : {1u, 2u, 4u}) {
    VkImageFormatProperties format;
    if (image_format_properties(physical[0], VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, &format)
        != VK_SUCCESS || !(format.sampleCounts & samples)) {
      std::cout << "Vulkan depth " << samples << "x unavailable\n"; continue;
    }
    const bool programmed = samples > 1 && device->extensions().ext_EXT_sample_locations &&
        (device->properties().sampleLocationSampleCounts & samples);
    VkSampleLocationEXT locations[4]{};
    int8_t positions[4][2]{};
    if (samples > 1) rex::graphics::draw_util::GetHostSampleXenosPositions(
        samples == 2 ? MsaaSamples::k2X : MsaaSamples::k4X, true, false, positions);
    for (unsigned i = 0; i < samples; ++i)
      locations[i] = {0.5f + positions[i][0] / 16.f, 0.5f + positions[i][1] / 16.f};
    VkSampleLocationsInfoEXT sample_info{VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT};
    sample_info.sampleLocationsPerPixel = VkSampleCountFlagBits(samples);
    sample_info.sampleLocationGridSize = {1, 1}; sample_info.sampleLocationsCount = samples;
    sample_info.pSampleLocations = locations;
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.flags = programmed ? VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT : 0;
    image_info.imageType = VK_IMAGE_TYPE_2D; image_info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    image_info.extent = {32, 32, 1}; image_info.mipLevels = image_info.arrayLayers = 1;
    image_info.samples = VkSampleCountFlagBits(samples); image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage image; Need(fn.vkCreateImage(vk, &image_info, nullptr, &image) == VK_SUCCESS, "depth image");
    VkMemoryRequirements requirements; fn.vkGetImageMemoryRequirements(vk, image, &requirements);
    uint32_t memory_type = 0;
    while (!(requirements.memoryTypeBits & (1u << memory_type))) ++memory_type;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = memory_type;
    VkDeviceMemory allocation_handle;
    Need(fn.vkAllocateMemory(vk, &allocation, nullptr, &allocation_handle) == VK_SUCCESS, "depth allocation");
    Need(fn.vkBindImageMemory(vk, image, allocation_handle, 0) == VK_SUCCESS, "depth binding");
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image; view_info.viewType = VK_IMAGE_VIEW_TYPE_2D; view_info.format = image_info.format;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
    VkImageView view; Need(fn.vkCreateImageView(vk, &view_info, nullptr, &view) == VK_SUCCESS, "depth view");
    VkAttachmentDescription attachment{}; attachment.format = image_info.format;
    attachment.samples = image_info.samples; attachment.loadOp = attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depth{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depth;
    VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass_info.attachmentCount = pass_info.subpassCount = 1;
    pass_info.pAttachments = &attachment; pass_info.pSubpasses = &subpass;
    VkRenderPass pass; Need(fn.vkCreateRenderPass(vk, &pass_info, nullptr, &pass) == VK_SUCCESS, "depth render pass");
    VkFramebufferCreateInfo fb_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.renderPass = pass; fb_info.attachmentCount = fb_info.layers = 1;
    fb_info.pAttachments = &view; fb_info.width = fb_info.height = 32;
    VkFramebuffer fb; Need(fn.vkCreateFramebuffer(vk, &fb_info, nullptr, &fb) == VK_SUCCESS, "depth framebuffer");
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = device->queue_family_graphics_compute();
    VkCommandPool pool; Need(fn.vkCreateCommandPool(vk, &pool_info, nullptr, &pool) == VK_SUCCESS, "command pool");
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = pool; command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    VkCommandBuffer command;
    Need(fn.vkAllocateCommandBuffers(vk, &command_info, &command) == VK_SUCCESS, "command buffer");
    VkCommandBufferBeginInfo command_begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Need(fn.vkBeginCommandBuffer(command, &command_begin) == VK_SUCCESS, "begin commands");
    VkAttachmentSampleLocationsEXT initial{0, sample_info};
    VkSubpassSampleLocationsEXT post{0, sample_info};
    VkRenderPassSampleLocationsBeginInfoEXT locations_begin{VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT};
    locations_begin.attachmentInitialSampleLocationsCount = locations_begin.postSubpassSampleLocationsCount = 1;
    locations_begin.pAttachmentInitialSampleLocations = &initial;
    locations_begin.pPostSubpassSampleLocations = &post;
    VkClearValue clear{}; clear.depthStencil = {0.25f, 0x5A};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.pNext = programmed ? &locations_begin : nullptr; begin.renderPass = pass; begin.framebuffer = fb;
    begin.renderArea.extent = {32, 32}; begin.clearValueCount = 1; begin.pClearValues = &clear;
    fn.vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE); fn.vkCmdEndRenderPass(command);
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.pNext = programmed ? &sample_info : nullptr;
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image; barrier.subresourceRange = view_info.subresourceRange;
    fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    Need(fn.vkEndCommandBuffer(command) == VK_SUCCESS, "end commands");
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
    Need(fn.vkCreateFence(vk, &fence_info, nullptr, &fence) == VK_SUCCESS, "fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    { auto queue = device->AcquireQueue(pool_info.queueFamilyIndex, 0);
      Need(fn.vkQueueSubmit(queue.queue(), 1, &submit, fence) == VK_SUCCESS, "submit"); }
    Need(fn.vkWaitForFences(vk, 1, &fence, VK_TRUE, 5'000'000'000ull) == VK_SUCCESS, "GPU completion within 5 seconds");
    std::cout << "Vulkan depth clear/transition " << samples << "x programmed=" << programmed << " passed\n";
    fn.vkDestroyFence(vk, fence, nullptr); fn.vkDestroyCommandPool(vk, pool, nullptr);
    fn.vkDestroyFramebuffer(vk, fb, nullptr); fn.vkDestroyRenderPass(vk, pass, nullptr);
    fn.vkDestroyImageView(vk, view, nullptr); fn.vkDestroyImage(vk, image, nullptr);
    fn.vkFreeMemory(vk, allocation_handle, nullptr);
  }
}

#ifdef _WIN32
void D3D12() {
  using Microsoft::WRL::ComPtr;
  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    std::cout << "D3D12 unavailable\n"; return;
  }
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options{};
  Need(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &options, sizeof(options))), "D3D12 options");
  std::cout << "D3D12 programmable sample positions tier=" << options.ProgrammableSamplePositionsTier << '\n';
  D3D12_COMMAND_QUEUE_DESC queue_desc{}; ComPtr<ID3D12CommandQueue> queue;
  Need(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))), "D3D12 queue");
  for (unsigned samples : {1u, 2u, 4u}) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality{DXGI_FORMAT_D24_UNORM_S8_UINT, samples};
    Need(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality, sizeof(quality))), "D3D12 MSAA support");
    if (!quality.NumQualityLevels) { std::cout << "D3D12 depth " << samples << "x unavailable\n"; continue; }
    ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> commands;
    Need(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))), "D3D12 allocator");
    Need(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands))), "D3D12 list");
    ComPtr<ID3D12GraphicsCommandList1> list1; commands.As(&list1);
    const bool programmed = samples > 1 && list1 && options.ProgrammableSamplePositionsTier != D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED;
    D3D12_SAMPLE_POSITION positions[4]{}; int8_t offsets[4][2]{};
    if (programmed) {
      rex::graphics::draw_util::GetHostSampleXenosPositions(samples == 2 ? MsaaSamples::k2X : MsaaSamples::k4X, true, false, offsets);
      for (unsigned i = 0; i < samples; ++i) positions[i] = {offsets[i][0], offsets[i][1]};
      list1->SetSamplePositions(samples, 1, positions);
    }
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 32; desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R24G8_TYPELESS; desc.SampleDesc.Count = samples;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> image;
    Need(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr, IID_PPV_ARGS(&image))), "D3D12 depth surface");
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; hd.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> dsv; Need(SUCCEEDED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv))), "DSV heap");
    D3D12_DEPTH_STENCIL_VIEW_DESC vd{}; vd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    vd.ViewDimension = samples == 1 ? D3D12_DSV_DIMENSION_TEXTURE2D : D3D12_DSV_DIMENSION_TEXTURE2DMS;
    device->CreateDepthStencilView(image.Get(), &vd, dsv->GetCPUDescriptorHandleForHeapStart());
    commands->ClearDepthStencilView(dsv->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.25f, 0x5A, 0, nullptr);
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {image.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        programmed ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    commands->ResourceBarrier(1, &barrier);
    if (programmed) {
      list1->ResolveSubresourceRegion(image.Get(), 0, 0, 0, image.Get(), 0, nullptr, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_RESOLVE_MODE_DECOMPRESS);
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      commands->ResourceBarrier(1, &barrier);
    }
    Need(SUCCEEDED(commands->Close()), "D3D12 close");
    ID3D12CommandList* lists[] = {commands.Get()}; queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence; Need(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))), "D3D12 fence");
    Need(SUCCEEDED(queue->Signal(fence.Get(), 1)), "D3D12 signal");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); Need(event != nullptr, "D3D12 event");
    Need(SUCCEEDED(fence->SetEventOnCompletion(1, event)), "D3D12 fence event");
    DWORD wait = WaitForSingleObject(event, 5000); CloseHandle(event);
    Need(wait == WAIT_OBJECT_0 && SUCCEEDED(device->GetDeviceRemovedReason()), "D3D12 GPU completion");
    std::cout << "D3D12 depth clear/transition " << samples << "x programmed=" << programmed << " passed\n";
  }
}
#endif
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--vulkan-xenos") { Vulkan(true); return 0; }
    Vulkan(false);
    Vulkan(true);
#ifdef _WIN32
    D3D12();
#endif
  } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
  return 0;
}
